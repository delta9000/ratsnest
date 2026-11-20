#include "engine/pipewire_out.h"

#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wconversion"
#pragma GCC diagnostic ignored "-Wsign-conversion"
#include <pipewire/pipewire.h>
#include <pipewire/stream.h>
#include <spa/node/io.h>
#include <spa/param/audio/format-utils.h>
#include <spa/pod/builder.h>
#pragma GCC diagnostic pop

#include <atomic>
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <mutex>

#include "engine/audio_safety.h"
#include "rt/denormal.h"

namespace ms::engine {

struct PipewireOut::Impl {
  RenderFn fn = nullptr;
  void* user = nullptr;
  pw_thread_loop* loop = nullptr;
  pw_stream* stream = nullptr;
  std::atomic<bool> ok{false};
  std::atomic<uint32_t> rate{0};
  std::atomic<uint64_t> xruns{0};
  // Set by io_changed (loop thread, lock held), read from process() (RT
  // thread). PipeWire owns the pointed-to spa_io_position for the stream's
  // lifetime once assigned; relaxed ordering is enough since this is a
  // best-effort xrun counter, not a synchronization point.
  std::atomic<spa_io_position*> position{nullptr};
  std::mutex err_mu;  // non-RT only (state_changed runs on the loop thread)
  std::string err;

  static void on_process(void* userdata) {
    auto* self = static_cast<Impl*>(userdata);
    rt::flush_denormals_on_this_thread();
    pw_buffer* b = pw_stream_dequeue_buffer(self->stream);
    if (b == nullptr) return;
    spa_buffer* buf = b->buffer;
    if (buf->n_datas >= 2 && buf->datas[0].data != nullptr &&
        buf->datas[1].data != nullptr) {
      // Both planes are written for `frames`: bound by the smaller one.
      const uint32_t cap =
          std::min(buf->datas[0].maxsize, buf->datas[1].maxsize) /
          static_cast<uint32_t>(sizeof(float));
      uint32_t want =
          b->requested != 0 ? static_cast<uint32_t>(b->requested) : cap;
      const uint32_t frames = want < cap ? want : cap;

      auto* position = self->position.load(std::memory_order_relaxed);
      if (position != nullptr &&
          (position->clock.flags & SPA_IO_CLOCK_FLAG_XRUN_RECOVER) != 0) {
        self->xruns.fetch_add(1, std::memory_order_relaxed);
      }

      auto* l = static_cast<float*>(buf->datas[0].data);
      auto* r = static_cast<float*>(buf->datas[1].data);
      self->fn(self->user, l, r, frames, position != nullptr ? position->clock.nsec : 0);
      clamp_device_buffer(l, r, frames);

      for (uint32_t c = 0; c < 2; ++c) {
        buf->datas[c].chunk->offset = 0;
        buf->datas[c].chunk->stride = static_cast<int32_t>(sizeof(float));
        buf->datas[c].chunk->size = frames * static_cast<uint32_t>(sizeof(float));
      }
    }
    pw_stream_queue_buffer(self->stream, b);
  }

  static void on_param_changed(void* userdata, uint32_t id,
                                const spa_pod* param) {
    auto* self = static_cast<Impl*>(userdata);
    if (id != SPA_PARAM_Format || param == nullptr) return;
    spa_audio_info_raw info{};
    if (spa_format_audio_raw_parse(param, &info) >= 0 && info.rate != 0) {
      self->rate.store(info.rate, std::memory_order_relaxed);
    }
  }

  static void on_io_changed(void* userdata, uint32_t id, void* area,
                             uint32_t size) {
    auto* self = static_cast<Impl*>(userdata);
    if (id == SPA_IO_Position && size >= sizeof(spa_io_position)) {
      self->position.store(static_cast<spa_io_position*>(area),
                            std::memory_order_relaxed);
    } else if (id == SPA_IO_Position) {
      self->position.store(nullptr, std::memory_order_relaxed);
    }
  }

  static void on_state_changed(void* userdata, pw_stream_state /*old*/,
                                pw_stream_state state, const char* error) {
    auto* self = static_cast<Impl*>(userdata);
    if (state == PW_STREAM_STATE_ERROR) {
      self->ok.store(false, std::memory_order_release);
      std::lock_guard<std::mutex> lk(self->err_mu);
      self->err = error != nullptr ? error : "unknown PipeWire error";
    } else if (state == PW_STREAM_STATE_UNCONNECTED) {
      // A silent server death (no ERROR, just UNCONNECTED) must also flip
      // ok() false, or the reconnect loop in cmd_play never fires.
      self->ok.store(false, std::memory_order_release);
    } else if (state == PW_STREAM_STATE_STREAMING ||
               state == PW_STREAM_STATE_PAUSED) {
      self->ok.store(true, std::memory_order_release);
    }
  }

  // Member order must match the installed pipewire/stream.h (version 2):
  // version, destroy, state_changed, control_info, io_changed, param_changed,
  // add_buffer, remove_buffer, process, drained, command, trigger_done.
  // Designated initializers make this robust against reordering (compiler
  // errors instead of silently wiring the wrong callback into the wrong
  // slot). Declared as a static member (rather than a free anonymous-
  // namespace object) so its out-of-class definition can reference the
  // private nested Impl type.
  static const pw_stream_events kEvents;
};

const pw_stream_events PipewireOut::Impl::kEvents = {
    .version = PW_VERSION_STREAM_EVENTS,
    .destroy = nullptr,
    .state_changed = PipewireOut::Impl::on_state_changed,
    .control_info = nullptr,
    .io_changed = PipewireOut::Impl::on_io_changed,
    .param_changed = PipewireOut::Impl::on_param_changed,
    .add_buffer = nullptr,
    .remove_buffer = nullptr,
    .process = PipewireOut::Impl::on_process,
    .drained = nullptr,
    .command = nullptr,
    .trigger_done = nullptr,
};

PipewireOut::PipewireOut(const char* app_name, RenderFn fn, void* user,
                          const char* latency, uint32_t requested_rate)
    : impl_(std::make_unique<Impl>()) {
  impl_->fn = fn;
  impl_->user = user;
  pw_init(nullptr, nullptr);
  impl_->loop = pw_thread_loop_new("ratsnest-audio", nullptr);
  if (impl_->loop == nullptr) {
    std::lock_guard<std::mutex> lk(impl_->err_mu);
    impl_->err = "pw_thread_loop_new failed";
    return;
  }

  pw_thread_loop_lock(impl_->loop);
  pw_properties* props = pw_properties_new(
      PW_KEY_MEDIA_TYPE, "Audio", PW_KEY_MEDIA_CATEGORY, "Playback",
      PW_KEY_MEDIA_ROLE, "Music", nullptr);
  if (latency != nullptr) pw_properties_set(props, PW_KEY_NODE_LATENCY, latency);
  if (requested_rate != 0) {
    char rate_str[32];
    std::snprintf(rate_str, sizeof(rate_str), "1/%u", requested_rate);
    pw_properties_set(props, PW_KEY_NODE_RATE, rate_str);
  }
  impl_->stream = pw_stream_new_simple(pw_thread_loop_get_loop(impl_->loop),
                                        app_name, props, &Impl::kEvents,
                                        impl_.get());
  if (impl_->stream != nullptr) {
    uint8_t pod_buffer[1024];
    spa_pod_builder b = SPA_POD_BUILDER_INIT(pod_buffer, sizeof(pod_buffer));
    spa_audio_info_raw raw = SPA_AUDIO_INFO_RAW_INIT(
        .format = SPA_AUDIO_FORMAT_F32P, .rate = requested_rate, .channels = 2,
        .position = {SPA_AUDIO_CHANNEL_FL, SPA_AUDIO_CHANNEL_FR});
    const spa_pod* params[1];
    params[0] = spa_format_audio_raw_build(&b, SPA_PARAM_EnumFormat, &raw);
    if (params[0] == nullptr ||
        pw_stream_connect(impl_->stream, PW_DIRECTION_OUTPUT, PW_ID_ANY,
                           static_cast<pw_stream_flags>(
                               PW_STREAM_FLAG_AUTOCONNECT |
                               PW_STREAM_FLAG_MAP_BUFFERS |
                               PW_STREAM_FLAG_RT_PROCESS),
                           params, 1) < 0) {
      std::lock_guard<std::mutex> lk(impl_->err_mu);
      impl_->err = "pw_stream_connect failed";
    }
  } else {
    std::lock_guard<std::mutex> lk(impl_->err_mu);
    impl_->err = "pw_stream_new_simple failed";
  }
  pw_thread_loop_unlock(impl_->loop);
  pw_thread_loop_start(impl_->loop);
}

PipewireOut::~PipewireOut() {
  if (impl_->loop != nullptr) {
    pw_thread_loop_stop(impl_->loop);  // without the lock (documented order)
    if (impl_->stream != nullptr) pw_stream_destroy(impl_->stream);
    pw_thread_loop_destroy(impl_->loop);
  }
  pw_deinit();
}

bool PipewireOut::ok() const {
  return impl_->ok.load(std::memory_order_acquire) && error().empty();
}

std::string PipewireOut::error() const {
  std::lock_guard<std::mutex> lk(impl_->err_mu);
  return impl_->err;
}

uint32_t PipewireOut::observed_rate() const {
  return impl_->rate.load(std::memory_order_relaxed);
}

uint64_t PipewireOut::xruns() const {
  return impl_->xruns.load(std::memory_order_relaxed);
}

}  // namespace ms::engine
