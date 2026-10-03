#include "dsp/modules.h"
#include <algorithm>
#include <cmath>
#include <cstdint>

// Concept-inspired by Mutable Instruments Branches' Bernoulli gate routing;
// no Branches firmware code was copied. See docs/attribution.md.

namespace ms::dsp {
namespace {
constexpr InputInfo kIns[] = {{"gate", 0.f},
                              {"prob", 0.5f},
                              {"reset", 0.f, false},
                              {"length", 0.f, false},
                              {"reroll", 0.f, false}};
constexpr ConfigInfo kCfg[] = {{"seed", nullptr}};
constexpr OutputInfo kOuts[] = {{"a"}, {"b"}, {"value"}};

class Bern final : public Module {
public:
  void init(double, const Config &cfg) override {
    double seed = cfg_num(cfg, "seed", 1.0);
    if (!std::isfinite(seed))
      seed = 1.0;
    state_ = static_cast<std::uint32_t>(
                 std::clamp(std::abs(seed), 0.0, 4294967295.0)) *
             2654435761u;
    if (state_ == 0)
      state_ = 0x9E3779B9u;
    seed_ = state_;
  }
  void process(const float *ins, float *outs) override {
    const bool reset = ins[2] > .5f, reroll = ins[4] > .5f;
    if (reroll && !prev_reroll_) {
      state_ = seed_;
      draw();
      seed_ = state_;
      position_ = 0;
    }
    if (reset && !prev_reset_) {
      state_ = seed_;
      position_ = 0;
    }
    prev_reset_ = reset;
    prev_reroll_ = reroll;
    const bool rising = ins[0] > 0.5f && prev_gate_ <= 0.5f;
    prev_gate_ = ins[0];
    if (rising) {
      const float p = std::clamp(ins[1], 0.0f, 1.0f);
      const int length =
          std::isfinite(ins[3])
              ? static_cast<int>(std::round(std::clamp(ins[3], 0.f, 65536.f)))
              : 0;
      if (length > 0 && position_ >= length) {
        state_ = seed_;
        position_ = 0;
      }
      // Consume one draw even at probability extremes: density changes must
      // not change random values at subsequent positions.
      value_ = draw();
      selected_a_ = p >= 1.f || value_ < p;
      if (length > 0)
        ++position_;
      else
        position_ = 0;
    }
    const bool high = ins[0] > 0.5f;
    outs[0] = high && selected_a_ ? 1.0f : 0.0f;
    outs[1] = high && !selected_a_ ? 1.0f : 0.0f;
    outs[2] = value_;
  }
  void copy_state_from(const Module &other) override {
    const auto &o = static_cast<const Bern &>(other);
    state_ = o.state_;
    seed_ = o.seed_;
    position_ = o.position_;
    value_ = o.value_;
    prev_reset_ = o.prev_reset_;
    prev_reroll_ = o.prev_reroll_;
    selected_a_ = o.selected_a_;
    prev_gate_ = o.prev_gate_;
  }

private:
  // Marsaglia xorshift32; see docs/attribution.md.
  float draw() {
    std::uint32_t x = state_;
    x ^= x << 13;
    x ^= x >> 17;
    x ^= x << 5;
    state_ = x;
    return static_cast<float>(x) * (1.0f / 4294967296.0f);
  }
  std::uint32_t state_ = 0x9E3779B9u;
  std::uint32_t seed_ = 0x9E3779B9u;
  int position_ = 0;
  float value_ = 0;
  bool prev_reset_ = false, prev_reroll_ = false;
  bool selected_a_ = false;
  float prev_gate_ = 0.0f;
};
std::unique_ptr<Module> make() { return std::make_unique<Bern>(); }
} // namespace
const ModuleInfo bern_info{"bern", kIns, kCfg, kOuts, &make};
} // namespace ms::dsp
