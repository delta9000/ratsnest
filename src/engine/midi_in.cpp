#include "engine/midi_in.h"

#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wconversion"
#pragma GCC diagnostic ignored "-Wsign-conversion"
#include <pipewire/pipewire.h>
#include <pipewire/filter.h>
#include <spa/control/control.h>
#include <spa/control/ump-utils.h>
#include <spa/pod/iter.h>
#include <spa/pod/pod.h>
#pragma GCC diagnostic pop

#include <atomic>
#include <cstdint>
#include <cstddef>
#include <mutex>

#include "dsp/module.h"
#include "engine/rt_runner.h"
#include "engine/pw_buffers.h"

// --- Step 1: payload format, verified on the build machine ---------------
// `grep -rn "SPA_CONTROL_UMP\|SPA_CONTROL_Midi" /usr/include/spa/` (headers
// live under /usr/include/spa-0.2/spa/control/) and `pkg-config --modversion
// libpipewire-0.3` (1.6.7) show BOTH control types declared:
// spa/control/control.h enumerates SPA_CONTROL_Midi ("spa_pod_bytes with raw
// midi data (deprecated, use SPA_CONTROL_UMP)") and SPA_CONTROL_UMP
// ("spa_pod_bytes with raw UMP ... native endian"). Since either can arrive
// depending on what the connected peer negotiates, the port is opened with
// the generic PW_KEY_FORMAT_DSP="8 bit raw midi" shortcut (no explicit
// SPA_TYPE_OBJECT_Format pod -- this is the same shortcut PipeWire's own
// MIDI bridges use) and the process callback below handles both control
// types in one loop, converting UMP MIDI1 channel-voice packets to 3-byte
// events via spa/control/ump-utils.h's spa_ump_to_midi (ships in this SDK).

namespace ms::engine {

struct MidiIn::Impl {
  RtRunner* runner = nullptr;
  pw_thread_loop* loop = nullptr;
  pw_filter* filter = nullptr;
  void* port = nullptr;
  // MIDI2-to-MIDI1 conversion state for spa_ump_to_midi (bank-select/program
  // change straddle multiple UMP packets); persists across process() calls
  // on the single midi RT thread, matching the ump-utils.h contract.
  uint64_t ump_state = 0;
  std::atomic<bool> ok{false};
  std::mutex err_mu;  // non-RT only (state_changed runs on the filter's loop thread)
  std::string err;

  // Parses a raw MIDI byte stream (status byte(s) plus data), expanding
  // running status, and pushes each complete channel-voice/system message
  // as one dsp::MidiEvent. Used for both SPA_CONTROL_Midi's raw bytes and
  // the per-message output of spa_ump_to_midi -- neither is guaranteed to
  // already be running-status-free, so this is the single normalization
  // point (module.h: "running status is expanded by the producer").
  static void on_process(void* userdata, spa_io_position* position) {
    auto* self = static_cast<Impl*>(userdata);
    pw_buffer* b = pw_filter_dequeue_buffer(self->port);
    if (b == nullptr) return;
    spa_buffer* buf = b->buffer;
    if (buf->n_datas >= 1 && buf->datas[0].data != nullptr) {
      const spa_data& d = buf->datas[0];
      pw::midi_from_pod(
          static_cast<const spa_pod*>(
              spa_pod_from_data(d.data, d.maxsize, d.chunk->offset, d.chunk->size)),
          position, self->ump_state, [self](const dsp::MidiEvent& e, uint64_t at) {
            self->runner->push_midi(e, at);  // drop-on-full; never blocks (RT rule 3)
          });
    }
    pw_filter_queue_buffer(self->port, b);
  }

  static void on_state_changed(void* userdata, pw_filter_state /*old*/,
                                pw_filter_state state, const char* error) {
    auto* self = static_cast<Impl*>(userdata);
    if (state == PW_FILTER_STATE_ERROR) {
      self->ok.store(false, std::memory_order_release);
      std::lock_guard<std::mutex> lk(self->err_mu);
      self->err = error != nullptr ? error : "unknown PipeWire error";
    } else if (state == PW_FILTER_STATE_UNCONNECTED) {
      self->ok.store(false, std::memory_order_release);
    } else if (state == PW_FILTER_STATE_STREAMING ||
               state == PW_FILTER_STATE_PAUSED) {
      self->ok.store(true, std::memory_order_release);
    }
  }

  // Member order must match the installed pipewire/filter.h: version,
  // destroy, io_changed, param_changed, add_buffer, remove_buffer, process,
  // drained, command. Designated initializers guard against reordering.
  static const pw_filter_events kEvents;
};

const pw_filter_events MidiIn::Impl::kEvents = {
    .version = PW_VERSION_FILTER_EVENTS,
    .destroy = nullptr,
    .state_changed = MidiIn::Impl::on_state_changed,
    .io_changed = nullptr,
    .param_changed = nullptr,
    .add_buffer = nullptr,
    .remove_buffer = nullptr,
    .process = MidiIn::Impl::on_process,
    .drained = nullptr,
    .command = nullptr,
};

MidiIn::MidiIn(const char* app_name, RtRunner& runner)
    : impl_(std::make_unique<Impl>()) {
  impl_->runner = &runner;
  pw_init(nullptr, nullptr);
  impl_->loop = pw_thread_loop_new("ratsnest-midi", nullptr);
  if (impl_->loop == nullptr) {
    std::lock_guard<std::mutex> lk(impl_->err_mu);
    impl_->err = "pw_thread_loop_new failed";
    return;
  }

  pw_thread_loop_lock(impl_->loop);
  pw_properties* props = pw_properties_new(
      PW_KEY_MEDIA_TYPE, "Midi", PW_KEY_MEDIA_CATEGORY, "Filter",
      PW_KEY_MEDIA_ROLE, "DSP", nullptr);
  impl_->filter = pw_filter_new_simple(pw_thread_loop_get_loop(impl_->loop),
                                        app_name, props, &Impl::kEvents,
                                        impl_.get());
  if (impl_->filter != nullptr) {
    impl_->port = pw_filter_add_port(
        impl_->filter, PW_DIRECTION_INPUT, PW_FILTER_PORT_FLAG_MAP_BUFFERS,
        /*port_data_size=*/0,
        pw_properties_new(PW_KEY_FORMAT_DSP, "8 bit raw midi",
                           PW_KEY_PORT_NAME, "midi-in", nullptr),
        nullptr, 0);
    if (impl_->port == nullptr ||
        pw_filter_connect(impl_->filter, PW_FILTER_FLAG_RT_PROCESS, nullptr,
                           0) < 0) {
      std::lock_guard<std::mutex> lk(impl_->err_mu);
      impl_->err = "pw_filter_add_port/connect failed";
    }
  } else {
    std::lock_guard<std::mutex> lk(impl_->err_mu);
    impl_->err = "pw_filter_new_simple failed";
  }
  pw_thread_loop_unlock(impl_->loop);
  pw_thread_loop_start(impl_->loop);
}

MidiIn::~MidiIn() {
  if (impl_->loop != nullptr) {
    pw_thread_loop_stop(impl_->loop);  // without the lock (documented order)
    if (impl_->filter != nullptr) pw_filter_destroy(impl_->filter);
    pw_thread_loop_destroy(impl_->loop);
  }
  pw_deinit();
}

bool MidiIn::ok() const {
  return impl_->ok.load(std::memory_order_acquire) && error().empty();
}

std::string MidiIn::error() const {
  std::lock_guard<std::mutex> lk(impl_->err_mu);
  return impl_->err;
}

}  // namespace ms::engine
