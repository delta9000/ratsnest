#include <algorithm>
#include <cmath>
#include "dsp/input_key.h"
#include "dsp/mastering.h"
#include "dsp/modules.h"

// Transient shaper by differential envelopes: a fast and a slow follower
// disagree at an onset (attack) and in a decay (sustain); the gap, in dB,
// scales the gain. Concept-generic; see docs/attribution.md.

namespace ms::dsp {
namespace {

constexpr InputInfo kIns[] = {{"in_l", 0.0f},   {"in_r", 0.0f},
                              {"attack", 0.0f},  // dB at the sharpest onset
                              {"sustain", 0.0f}};  // dB deep in a tail
constexpr OutputInfo kOuts[] = {{"out_l"}, {"out_r"}, {"gain"}};
constexpr float kFull = 12.0f;  // envelope gap (dB) that counts as "full"

// Peak follower with separate attack and release coefficients.
struct Follower {
  float a = 1.0f, r = 1.0f, env = 0.0f;
  void set(float attack_s, float release_s, float rate) {
    a = 1.0f - std::exp(-1.0f / (attack_s * rate));
    r = 1.0f - std::exp(-1.0f / (release_s * rate));
  }
  float push(float x) {
    env = flush_denormal(env + (x > env ? a : r) * (x - env));
    return env;
  }
};

class Transient final : public Module {
 public:
  void init(double rate, const Config&) override {
    const auto hz = static_cast<float>(rate);
    // attack: 0.5 ms vs 20 ms rise, same 100 ms fall.
    // sustain: same 0.5 ms rise, 20 ms vs 300 ms fall.
    fast_.set(0.0005f, 0.1f, hz);
    slow_.set(0.02f, 0.1f, hz);
    short_.set(0.0005f, 0.02f, hz);
    long_.set(0.0005f, 0.3f, hz);
    fast_.env = slow_.env = short_.env = long_.env = 0.0f;
    hold_len_ = std::max(1, hold_samples(0.02f, hz));
    hold_ = {};
  }

  void process(const float* ins, float* outs) override {
    const float l = sanitize(ins[0], 0.0f), r = sanitize(ins[1], 0.0f);
    const float attack = sanitize_clamp(ins[2], -24.0f, 24.0f, 0.0f);
    const float sustain = sanitize_clamp(ins[3], -24.0f, 24.0f, 0.0f);
    // Peak-held 20 ms, so the followers see a level rather than the
    // troughs of a waveform (the gaps would otherwise ripple on bass).
    const float x = hold_.push(std::max(std::abs(l), std::abs(r)), hold_len_);
    const float f = fast_.push(x), s = slow_.push(x), sh = short_.push(x), lg = long_.push(x);
    float g = 1.0f;
    if (attack != 0.0f || sustain != 0.0f) {
      // Gaps in dB, each 0..kFull, then scaled by the knobs.
      const float onset = std::clamp(linear_to_db((f + 1e-9f) / (s + 1e-9f)), 0.0f, kFull);
      const float tail = std::clamp(linear_to_db((lg + 1e-9f) / (sh + 1e-9f)), 0.0f, kFull);
      const float db = (attack * onset + sustain * tail) / kFull;
      if (gain_key_.changed({db})) g_ = db_to_linear(db);
      g = g_;
    }
    outs[0] = l * g;
    outs[1] = r * g;
    outs[2] = g;
  }

  void copy_state_from(const Module& other) override {
    const auto& o = static_cast<const Transient&>(other);
    fast_.env = o.fast_.env;
    slow_.env = o.slow_.env;
    short_.env = o.short_.env;
    long_.env = o.long_.env;
    hold_ = o.hold_;
  }

 private:
  Follower fast_, slow_, short_, long_;
  SlidingMax<4096> hold_;  // 20 ms up to 192 kHz
  int hold_len_ = 960;
  InputKey<1> gain_key_;
  float g_ = 1.0f;
};

std::unique_ptr<Module> make() { return std::make_unique<Transient>(); }
}  // namespace

const ModuleInfo transient_info{"transient", kIns, {}, kOuts, &make};
}  // namespace ms::dsp
