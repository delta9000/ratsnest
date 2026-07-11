#pragma once
#include <array>
#include <atomic>
#include <cstdint>
#include <type_traits>
#include "engine/engine.h"
#include "rt/spsc.h"

namespace ms::engine {

struct Command {
  enum class Kind : uint32_t { SetParam, Swap, Watch };
  Kind kind = Kind::SetParam;
  uint32_t node = 0, slot = 0;
  float value = 0.0f;
  Engine* engine = nullptr;  // Swap: ownership passes to the runner
};
static_assert(std::is_trivially_copyable_v<Command>);

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
  bool push(const Command& c) { return commands_.try_push(c); }
  bool try_retire(Engine*& e) { return retired_.try_pop(e); }
  float peak_l() const { return peak_l_.load(std::memory_order_acquire); }
  float peak_r() const { return peak_r_.load(std::memory_order_acquire); }
  bool try_watch_value(float& v) { return watch_.try_pop(v); }

  // --- RT side ---
  void render(float* l, float* r, size_t frames);

 private:
  void drain_commands();
  void render_block(float* l, float* r, size_t frames);

  double rate_;
  size_t glide_samples_;  // 20ms
  size_t fade_samples_;   // 50ms
  rt::SpscQueue<Command, 1024> commands_;
  rt::SpscQueue<Engine*, 8> retired_;
  rt::SpscQueue<float, 4096> watch_;
  Engine* current_ = nullptr;
  Engine* old_ = nullptr;  // non-null while crossfading
  size_t fade_pos_ = 0;
  uint32_t watch_node_ = kWatchOff, watch_slot_ = 0;
  std::atomic<float> peak_l_{0.0f}, peak_r_{0.0f};
  float env_l_ = 0.0f, env_r_ = 0.0f;
  std::array<float, kMaxBlock> scratch_l_{}, scratch_r_{};
};

}  // namespace ms::engine
