#pragma once
#include <memory>
#include <string>

namespace ms::engine {

class RtRunner;

// PipeWire MIDI input transport (docs/superpowers/specs/2026-07-11-midi-design.md
// §3-4). Owns its own pw_filter with a single control input port named
// "midi-in" (media type application/control, negotiated via the
// PW_KEY_FORMAT_DSP "8 bit raw midi" shortcut so PipeWire picks whichever
// wire format -- SPA_CONTROL_Midi or SPA_CONTROL_UMP -- the connected peer
// supports; the process callback handles both, see midi_in.cpp). The filter
// callback runs on PipeWire's own RT data thread, distinct from the audio
// thread driving RtRunner::render(); it parses the control buffer and pushes
// events into RtRunner's producer-side ring (RtRunner::push_midi) -- no
// allocation, no locks, no I/O, drop-on-full.
//
// Lifecycle: constructed once by the play path (src/main.cpp) alongside the
// PipewireOut audio stream, and lives for the whole play session. A patch
// hot-reload swaps engines inside RtRunner; it never touches this object, so
// the port and any patchbay connection to a controller survive edits (midi
// spec §4). The offline render path never constructs a MidiIn.
class MidiIn {
 public:
  // app_name is the PipeWire node name (distinct from the audio node so both
  // show up separately under `pw-cli ls Node`). runner must outlive this
  // object; push_midi is called from the PipeWire midi thread, which is a
  // different thread than the one calling RtRunner::render() -- satisfying
  // the SPSC ring's one-producer/one-consumer contract.
  MidiIn(const char* app_name, RtRunner& runner);
  ~MidiIn();
  MidiIn(const MidiIn&) = delete;
  MidiIn& operator=(const MidiIn&) = delete;

  // True once the filter is actually streaming/paused. PipeWire connects
  // asynchronously, so this legitimately reads false for a brief window
  // right after construction even on success -- do not gate a one-shot
  // startup check on it; poll it if you need a liveness check over time.
  bool ok() const;
  // Non-empty only on a definite failure: the port/filter was refused at
  // connect time (synchronous, so this is already populated by the time the
  // constructor returns) or the filter later entered PW_FILTER_STATE_ERROR.
  // Safe to check once, immediately after construction, to guard a startup
  // note -- callers must keep running audio-only on failure, never treat it
  // as fatal.
  std::string error() const;

 private:
  struct Impl;
  std::unique_ptr<Impl> impl_;
};

}  // namespace ms::engine
