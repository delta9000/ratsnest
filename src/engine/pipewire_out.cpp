#include "engine/pipewire_out.h"

#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wconversion"
#pragma GCC diagnostic ignored "-Wsign-conversion"
#include <pipewire/filter.h>
#include <pipewire/pipewire.h>
#include <spa/node/io.h>
#pragma GCC diagnostic pop

#include <atomic>
#include <cstdio>
#include <mutex>

#include "rt/denormal.h"

namespace ms::engine {

struct PipewireOut::Impl {
  RenderFn fn = nullptr;
  void* user = nullptr;
  pw_thread_loop* loop = nullptr;
  pw_filter* filter = nullptr;
  void* port_l = nullptr;  // pw-owned port-data handles (opaque to us)
  void* port_r = nullptr;
  std::atomic<bool> ok{false};
  std::atomic<uint32_t> rate{0};
  std::atomic<uint64_t> xruns{0};
  std::mutex err_mu;  // non-RT only (state_changed runs on the loop thread)
  std::string err;

  static void on_process(void* userdata, spa_io_position* position) {
    auto* self = static_cast<Impl*>(userdata);
    rt::flush_denormals_on_this_thread();
    const auto n = static_cast<uint32_t>(position->clock.duration);
    self->rate.store(position->clock.rate.denom, std::memory_order_relaxed);
    if ((position->clock.flags & SPA_IO_CLOCK_FLAG_XRUN_RECOVER) != 0)
      self->xruns.fetch_add(1, std::memory_order_relaxed);
    auto* l = static_cast<float*>(pw_filter_get_dsp_buffer(self->port_l, n));
    auto* r = static_cast<float*>(pw_filter_get_dsp_buffer(self->port_r, n));
    if (l == nullptr || r == nullptr) return;  // port not negotiated this cycle
    self->fn(self->user, l, r, n);
  }

  static void on_state_changed(void* userdata, pw_filter_state /*old*/,
                                pw_filter_state state, const char* error) {
    auto* self = static_cast<Impl*>(userdata);
    if (state == PW_FILTER_STATE_ERROR) {
      self->ok.store(false, std::memory_order_release);
      std::lock_guard<std::mutex> lk(self->err_mu);
      self->err = error != nullptr ? error : "unknown PipeWire error";
    } else if (state == PW_FILTER_STATE_STREAMING ||
               state == PW_FILTER_STATE_PAUSED) {
      self->ok.store(true, std::memory_order_release);
    }
  }

  // Member order must match the installed pipewire/filter.h (1.6.7):
  // version, destroy, state_changed, io_changed, param_changed, add_buffer,
  // remove_buffer, process, drained, command. Designated initializers make
  // this robust against reordering (compiler errors instead of silently
  // wiring the wrong callback into the wrong slot). Declared as a static
  // member (rather than a free anonymous-namespace object) so its
  // out-of-class definition can reference the private nested Impl type.
  static const pw_filter_events kEvents;
};

const pw_filter_events PipewireOut::Impl::kEvents = {
    .version = PW_VERSION_FILTER_EVENTS,
    .destroy = nullptr,
    .state_changed = PipewireOut::Impl::on_state_changed,
    .io_changed = nullptr,
    .param_changed = nullptr,
    .add_buffer = nullptr,
    .remove_buffer = nullptr,
    .process = PipewireOut::Impl::on_process,
    .drained = nullptr,
    .command = nullptr,
};

namespace {

void* add_port(pw_filter* f, const char* name, const char* channel) {
  return pw_filter_add_port(
      f, PW_DIRECTION_OUTPUT, PW_FILTER_PORT_FLAG_MAP_BUFFERS, 0,
      pw_properties_new(PW_KEY_FORMAT_DSP, "32 bit float mono audio",
                         PW_KEY_PORT_NAME, name, PW_KEY_AUDIO_CHANNEL, channel,
                         nullptr),
      nullptr, 0);
}

}  // namespace

PipewireOut::PipewireOut(const char* app_name, RenderFn fn, void* user,
                          const char* latency, uint32_t requested_rate)
    : impl_(std::make_unique<Impl>()) {
  impl_->fn = fn;
  impl_->user = user;
  pw_init(nullptr, nullptr);
  impl_->loop = pw_thread_loop_new("modsynth-audio", nullptr);
  if (impl_->loop == nullptr) return;

  pw_thread_loop_lock(impl_->loop);
  pw_properties* props = pw_properties_new(
      PW_KEY_MEDIA_TYPE, "Audio", PW_KEY_MEDIA_CATEGORY, "Playback",
      PW_KEY_MEDIA_ROLE, "Music", PW_KEY_MEDIA_CLASS, "Stream/Output/Audio",
      PW_KEY_NODE_AUTOCONNECT, "true", nullptr);
  if (latency != nullptr) pw_properties_set(props, PW_KEY_NODE_LATENCY, latency);
  if (requested_rate != 0) {
    char rate_str[32];
    std::snprintf(rate_str, sizeof(rate_str), "1/%u", requested_rate);
    pw_properties_set(props, PW_KEY_NODE_RATE, rate_str);
  }
  impl_->filter = pw_filter_new_simple(pw_thread_loop_get_loop(impl_->loop),
                                        app_name, props, &Impl::kEvents,
                                        impl_.get());
  if (impl_->filter != nullptr) {
    impl_->port_l = add_port(impl_->filter, "out_l", "FL");
    impl_->port_r = add_port(impl_->filter, "out_r", "FR");
    if (pw_filter_connect(impl_->filter, PW_FILTER_FLAG_RT_PROCESS, nullptr,
                           0) < 0) {
      std::lock_guard<std::mutex> lk(impl_->err_mu);
      impl_->err = "pw_filter_connect failed";
    }
  } else {
    std::lock_guard<std::mutex> lk(impl_->err_mu);
    impl_->err = "pw_filter_new_simple failed";
  }
  pw_thread_loop_unlock(impl_->loop);
  pw_thread_loop_start(impl_->loop);
}

PipewireOut::~PipewireOut() {
  if (impl_->loop != nullptr) {
    pw_thread_loop_stop(impl_->loop);  // without the lock (documented order)
    if (impl_->filter != nullptr) pw_filter_destroy(impl_->filter);
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
