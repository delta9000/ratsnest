#pragma once
#include <cstdint>
#include <memory>
#include <string>

namespace ms::engine {

class RtRunner;

// PipeWire audio capture transport for the `extin` module. Owns a pw_stream
// input (media category Capture, mono float32) on its own thread-loop; its RT
// process callback reads the captured buffer and pushes it as fixed AudioChunks
// into RtRunner's audio ring (RtRunner::push_audio) -- no allocation, no locks,
// no I/O, drop-on-full. The capture thread is distinct from the audio thread
// that calls RtRunner::render(), satisfying the SPSC one-producer/one-consumer
// contract.
//
// Lifecycle mirrors MidiIn: constructed once by the play path alongside the
// PipewireOut audio stream, and lives for the whole session so the capture port
// (and any patchbay connection) survives hot-reload. The offline render path
// never constructs an AudioIn, so `extin` outputs silence there.
class AudioIn {
 public:
  // runner must outlive this object; push_audio is called from the PipeWire
  // capture thread. requested_rate (0 = let PipeWire choose) matches the audio
  // output rate so capture and playback share a clock domain where possible.
  AudioIn(const char* app_name, RtRunner& runner, uint32_t requested_rate);
  ~AudioIn();
  AudioIn(const AudioIn&) = delete;
  AudioIn& operator=(const AudioIn&) = delete;

  // Same semantics as MidiIn: ok() races the async connect handshake (poll it);
  // error() is set only on a definite failure and is safe to check once right
  // after construction. A refused port is non-fatal -- keep playing.
  bool ok() const;
  std::string error() const;

 private:
  struct Impl;
  std::unique_ptr<Impl> impl_;
};

}  // namespace ms::engine
