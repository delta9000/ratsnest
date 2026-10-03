#pragma once
#include <array>
#include <atomic>
#include <cstdint>
#include <type_traits>
#include "dsp/module.h"
#include "engine/engine.h"
#include "rt/spsc.h"

namespace ms::engine {

struct Command {
  enum class Kind : uint32_t { SetParam, Swap, Watch, Observe };
  Kind kind = Kind::SetParam;
  uint32_t node = 0, slot = 0;
  float value = 0.0f;
  Engine* engine = nullptr;  // Swap: ownership passes to the runner
};
static_assert(std::is_trivially_copyable_v<Command>);

// A fixed-size chunk of captured external audio, trivially copyable so it can
// live in an rt::SpscQueue. The PipeWire capture thread fills these and pushes
// them via RtRunner::push_audio; the audio thread drains them into the live
// engine at block start. `len` is the number of valid samples in `s`.
struct AudioChunk {
  float s[64];
  uint32_t len;
};

// Bounded, lossy telemetry. The audio thread never waits for a reader.
struct Observation {
  uint64_t generation = 0;
  uint64_t frame = 0;
  size_t size = 0;
  std::array<float, 4096> values{};
};

// RT-side state machine. render() is the only method the audio thread calls;
// everything it does is allocation-, lock-, and I/O-free. Engines enter via
// Swap commands and leave via the retire queue (deleted by the non-RT side).
class RtRunner {
 public:
  static constexpr size_t kMaxBlock = 8192;
  static constexpr uint32_t kWatchOff = 0xffffffffu;

  explicit RtRunner(double sample_rate);
  ~RtRunner();
  RtRunner(const RtRunner&) = delete;
  RtRunner& operator=(const RtRunner&) = delete;

  // --- non-RT side ---
  size_t command_capacity() const { return commands_.write_available(); }
  bool push(const Command& c) { return commands_.try_push(c); }
  bool try_retire(Engine*& e) { return retired_.try_pop(e); }
  float peak_l() const { return peak_l_.load(std::memory_order_acquire); }
  float peak_r() const { return peak_r_.load(std::memory_order_acquire); }
  bool try_observe(Observation& v) { return observations_.try_pop(v); }
  bool try_watch_value(float& v) { return watch_.try_pop(v); }

  // Producer side of the MIDI ring: lock-free, allocation-free. Called from
  // Task 9's PipeWire midi-in callback, which runs on its own RT thread,
  // distinct from the audio thread that calls render() -- exactly the one-
  // producer/one-consumer contract rt::SpscQueue requires. Returns false and
  // drops the event if the ring is full (drop-newest: the events already
  // queued keep their place in line rather than being evicted).
  //
  // The ring lives here, on RtRunner, rather than on Engine: a topology
  // hot-reload swaps in a new Engine via a Swap command, and RtRunner is the
  // one object that outlives every such swap, so events queued right at a
  // swap are never silently dropped (midi spec §4).
  bool push_midi(const dsp::MidiEvent& e) { return midi_ring_.try_push(e); }

  // Producer side of the audio-capture ring, mirroring push_midi: called from
  // the PipeWire capture thread (a distinct thread from the audio thread that
  // calls render(), satisfying the SPSC one-producer/one-consumer contract).
  // Drop-on-full; never blocks. Lives on RtRunner so it survives engine swaps.
  bool push_audio(const AudioChunk& c) { return audio_ring_.try_push(c); }

  // --- RT side ---
  void render(float* l, float* r, size_t frames);

 private:
  void drain_commands();
  void render_block(float* l, float* r, size_t frames);

  rt::SpscQueue<Observation, 4> observations_;
  Observation observation_;
  bool observing_ = false;
  uint64_t generation_ = 0, frame_ = 0;
  size_t since_observation_ = 0;
  double rate_;
  size_t glide_samples_;  // 20ms
  size_t fade_samples_;   // 50ms
  rt::SpscQueue<Command, 1024> commands_;
  rt::SpscQueue<Engine*, 8> retired_;
  rt::SpscQueue<float, 4096> watch_;
  rt::SpscQueue<dsp::MidiEvent, 256> midi_ring_;
  rt::SpscQueue<AudioChunk, 128> audio_ring_;
  Engine* current_ = nullptr;
  Engine* old_ = nullptr;  // non-null while crossfading
  size_t fade_pos_ = 0;
  uint32_t watch_node_ = kWatchOff, watch_slot_ = 0;
  std::atomic<float> peak_l_{0.0f}, peak_r_{0.0f};
  float env_l_ = 0.0f, env_r_ = 0.0f;
  std::array<float, kMaxBlock> scratch_l_{}, scratch_r_{};
};

}  // namespace ms::engine
