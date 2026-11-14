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

namespace {

// Channel-voice / system message length in bytes given a status byte, or 0
// if unsupported (sysex and friends -- not handled; see module.h's
// MidiEvent comment: only 1..3-byte channel-voice events are carried).
size_t midi_message_length(uint8_t status) {
  switch (status & 0xf0) {
    case 0x80: case 0x90: case 0xa0: case 0xb0: case 0xe0: return 3;
    case 0xc0: case 0xd0: return 2;
    default: break;
  }
  switch (status) {
    case 0xf1: case 0xf3: return 2;
    case 0xf2: return 3;
    case 0xf6: return 1;
    default:
      if (status >= 0xf8) return 1;  // system realtime: single byte
      return 0;                      // sysex (0xf0) and anything else: unsupported
  }
}

}  // namespace

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
  void push_raw(const uint8_t* data, size_t len, uint64_t at_ns) {
    size_t i = 0;
    uint8_t running = 0;
    while (i < len) {
      uint8_t b0 = data[i];
      uint8_t status;
      size_t data_start;
      if ((b0 & 0x80) != 0) {
        status = b0;
        data_start = i + 1;
        running = status < 0xf0 ? status : 0;  // system messages clear running status
      } else {
        status = running;
        data_start = i;
        if (status == 0) { ++i; continue; }  // stray data byte, no running-status context
      }
      const size_t msg_len = midi_message_length(status);
      if (msg_len == 0) return;  // unsupported (e.g. sysex): drop the remainder, never block
      if (data_start + (msg_len - 1) > len) return;  // incomplete tail: drop
      dsp::MidiEvent e{};
      e.data[0] = status;
      for (size_t k = 1; k < msg_len; ++k) e.data[k] = data[data_start + k - 1];
      e.len = static_cast<uint8_t>(msg_len);
      runner->push_midi(e, at_ns);  // drop-on-full; never blocks (RT rule 3)
      i = data_start + (msg_len - 1);
    }
  }

  // When an event at frame `offset` of this cycle should sound: one cycle
  // later, at the same offset. The audio stream has usually rendered this
  // cycle already, so the next one is the earliest every event can make;
  // the fixed delay keeps the spacing the notes were played with.
  static uint64_t at_ns(const spa_io_position* p, uint32_t offset) {
    if (p == nullptr || p->clock.rate.denom == 0) return 0;  // unknown: next block
    const double ns_per_frame = 1e9 * p->clock.rate.num / p->clock.rate.denom;
    return p->clock.nsec +
           static_cast<uint64_t>(static_cast<double>(offset + p->clock.duration) * ns_per_frame);
  }

  void handle_control(const spa_pod_control* c, const spa_io_position* p) {
    const uint64_t at = at_ns(p, c->offset);
    if (c->type == SPA_CONTROL_Midi) {
      const auto* bytes = static_cast<const uint8_t*>(SPA_POD_BODY(&c->value));
      push_raw(bytes, SPA_POD_BODY_SIZE(&c->value), at);
    } else if (c->type == SPA_CONTROL_UMP) {
      const auto* words = static_cast<const uint32_t*>(SPA_POD_BODY(&c->value));
      size_t remaining = SPA_POD_BODY_SIZE(&c->value);
      while (remaining >= 4) {
        uint8_t midi_buf[8];
        int n = spa_ump_to_midi(&words, &remaining, midi_buf, sizeof(midi_buf), &ump_state);
        if (n < 0) break;       // malformed/oversized packet: drop the rest
        if (n > 0) push_raw(midi_buf, static_cast<size_t>(n), at);
      }
    }
    // Other control types (Properties, OSC) are not MIDI: ignored.
  }

  static void on_process(void* userdata, spa_io_position* position) {
    auto* self = static_cast<Impl*>(userdata);
    pw_buffer* b = pw_filter_dequeue_buffer(self->port);
    if (b == nullptr) return;
    spa_buffer* buf = b->buffer;
    if (buf->n_datas >= 1 && buf->datas[0].data != nullptr) {
      spa_data& d = buf->datas[0];
      const auto* pod = static_cast<const spa_pod*>(
          spa_pod_from_data(d.data, d.maxsize, d.chunk->offset, d.chunk->size));
      if (pod != nullptr && spa_pod_is_sequence(pod) != 0) {
        const auto* seq = reinterpret_cast<const spa_pod_sequence*>(pod);
        const spa_pod_control* c;
        SPA_POD_SEQUENCE_FOREACH(seq, c) self->handle_control(c, position);
      }
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
