#include "engine/audio_in.h"

#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wconversion"
#pragma GCC diagnostic ignored "-Wsign-conversion"
#include <pipewire/pipewire.h>
#include <pipewire/stream.h>
#include <spa/param/audio/format-utils.h>
#include <spa/pod/builder.h>
#pragma GCC diagnostic pop

#include <atomic>
#include <cstdint>
#include <mutex>

#include "engine/rt_runner.h"
#include "rt/denormal.h"

namespace ms::engine {

struct AudioIn::Impl {
  RtRunner* runner = nullptr;
  pw_thread_loop* loop = nullptr;
  pw_stream* stream = nullptr;
  std::atomic<bool> ok{false};
  std::mutex err_mu;
  std::string err;

  // RT capture callback: read the captured mono float buffer and push it into
  // the runner's audio ring in fixed 64-sample chunks. No alloc, no locks, no
  // I/O; drop-on-full inside push_audio.
  static void on_process(void* userdata) {
    auto* self = static_cast<Impl*>(userdata);
    rt::flush_denormals_on_this_thread();
    pw_buffer* b = pw_stream_dequeue_buffer(self->stream);
    if (b == nullptr) return;
    spa_buffer* buf = b->buffer;
    if (buf->n_datas >= 1 && buf->datas[0].data != nullptr) {
      const uint32_t offset = buf->datas[0].chunk->offset;
      const uint32_t size = buf->datas[0].chunk->size;
      const uint32_t stride = buf->datas[0].chunk->stride > 0
          ? static_cast<uint32_t>(buf->datas[0].chunk->stride)
          : static_cast<uint32_t>(sizeof(float));
      const uint32_t n = size / stride;
      const auto* base = static_cast<const uint8_t*>(buf->datas[0].data) + offset;
      const auto* p = reinterpret_cast<const float*>(base);

      AudioChunk chunk;
      uint32_t i = 0;
      while (i < n) {
        uint32_t take = n - i;
        if (take > 64) take = 64;
        for (uint32_t k = 0; k < take; ++k) chunk.s[k] = p[i + k];
        chunk.len = take;
        self->runner->push_audio(chunk);
        i += take;
      }
    }
    pw_stream_queue_buffer(self->stream, b);
  }

  static void on_state_changed(void* userdata, pw_stream_state /*old*/,
                                pw_stream_state state, const char* error) {
    auto* self = static_cast<Impl*>(userdata);
    if (state == PW_STREAM_STATE_ERROR) {
      self->ok.store(false, std::memory_order_release);
      std::lock_guard<std::mutex> lk(self->err_mu);
      self->err = error != nullptr ? error : "unknown PipeWire error";
    } else if (state == PW_STREAM_STATE_UNCONNECTED) {
      self->ok.store(false, std::memory_order_release);
    } else if (state == PW_STREAM_STATE_STREAMING ||
               state == PW_STREAM_STATE_PAUSED) {
      self->ok.store(true, std::memory_order_release);
    }
  }

  static const pw_stream_events kEvents;
};

const pw_stream_events AudioIn::Impl::kEvents = {
    .version = PW_VERSION_STREAM_EVENTS,
    .destroy = nullptr,
    .state_changed = AudioIn::Impl::on_state_changed,
    .control_info = nullptr,
    .io_changed = nullptr,
    .param_changed = nullptr,
    .add_buffer = nullptr,
    .remove_buffer = nullptr,
    .process = AudioIn::Impl::on_process,
    .drained = nullptr,
    .command = nullptr,
    .trigger_done = nullptr,
};

AudioIn::AudioIn(const char* app_name, RtRunner& runner, uint32_t requested_rate)
    : impl_(std::make_unique<Impl>()) {
  impl_->runner = &runner;
  pw_init(nullptr, nullptr);
  impl_->loop = pw_thread_loop_new("modsynth-capture", nullptr);
  if (impl_->loop == nullptr) {
    std::lock_guard<std::mutex> lk(impl_->err_mu);
    impl_->err = "pw_thread_loop_new failed";
    return;
  }

  pw_thread_loop_lock(impl_->loop);
  pw_properties* props = pw_properties_new(
      PW_KEY_MEDIA_TYPE, "Audio", PW_KEY_MEDIA_CATEGORY, "Capture",
      PW_KEY_MEDIA_ROLE, "Music", nullptr);
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
        .format = SPA_AUDIO_FORMAT_F32, .rate = requested_rate, .channels = 1);
    const spa_pod* params[1];
    params[0] = spa_format_audio_raw_build(&b, SPA_PARAM_EnumFormat, &raw);
    if (params[0] == nullptr ||
        pw_stream_connect(impl_->stream, PW_DIRECTION_INPUT, PW_ID_ANY,
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

AudioIn::~AudioIn() {
  if (impl_->loop != nullptr) {
    pw_thread_loop_stop(impl_->loop);
    if (impl_->stream != nullptr) pw_stream_destroy(impl_->stream);
    pw_thread_loop_destroy(impl_->loop);
  }
  pw_deinit();
}

bool AudioIn::ok() const {
  return impl_->ok.load(std::memory_order_acquire) && error().empty();
}

std::string AudioIn::error() const {
  std::lock_guard<std::mutex> lk(impl_->err_mu);
  return impl_->err;
}

}  // namespace ms::engine
