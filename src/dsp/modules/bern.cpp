#include "dsp/modules.h"
#include "dsp/util.h"
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
    state_ = cfg_seed(cfg);
    seed_ = state_;
  }
  void process(const float *ins, float *outs) override {
    const bool reset = ins[2] > .5f, reroll = ins[4] > .5f;
    if (reroll && !prev_reroll_) {
      state_ = seed_;
      xorshift32(state_);
      seed_ = state_;
      position_ = 0;
    }
    if (reset && !prev_reset_) {
      state_ = seed_;
      position_ = 0;
    }
    prev_reset_ = reset;
    prev_reroll_ = reroll;
    if (gate_edge_(ins[0]))
      choose(ins[1], round_clamp(ins[3], 0, 65536, 0));
    const bool high = ins[0] > 0.5f;
    outs[0] = as_gate(high && selected_a_);
    outs[1] = as_gate(high && !selected_a_);
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
    gate_edge_ = o.gate_edge_;
  }

private:
  // One coin toss. A positive `length` loops the sequence of tosses every
  // `length` gates.
  void choose(float prob, int length) {
    const float p = std::clamp(prob, 0.0f, 1.0f);
    if (length > 0 && position_ >= length) {
      state_ = seed_;
      position_ = 0;
    }
    // Consume one draw even at probability extremes: density changes must
    // not change random values at subsequent positions.
    value_ = unit_float(xorshift32(state_));
    selected_a_ = p >= 1.f || value_ < p;
    position_ = length > 0 ? position_ + 1 : 0;
  }
  std::uint32_t state_ = 0x9E3779B9u;
  std::uint32_t seed_ = 0x9E3779B9u;
  int position_ = 0;
  float value_ = 0;
  bool prev_reset_ = false, prev_reroll_ = false;
  bool selected_a_ = false;
  RisingEdge gate_edge_;
};
std::unique_ptr<Module> make() { return std::make_unique<Bern>(); }
} // namespace
const ModuleInfo bern_info{"bern", kIns, kCfg, kOuts, &make};
} // namespace ms::dsp
