#include <algorithm>
#include <cmath>
#include <numbers>
#include "dsp/input_key.h"
#include "dsp/modules.h"
#include "dsp/util.h"

namespace ms::dsp {
namespace {

constexpr InputInfo kIns[] = {{"in", 0.f},
                              {"cv", 0.f},
                              {"strike", 0.f},
                              {"damp", 0.5f},
                              {"decay", 0.25f}};
constexpr OutputInfo kOuts[] = {{"out"}};
// A strike ramps the gate fully open over this long (linear), then decays.
// Opening instantly stepped the output by up to ~0.2 on bass (the closed
// 30 Hz filter still carries the low end); 2 ms keeps the pluck percussive.
constexpr float kStrikeAttack = 0.002f;

class Lpg final : public Module {
 public:
  void init(double rate, const Config&) override {
    rate_ = static_cast<float>(rate);
    strike_step_ = 1.0f / (kStrikeAttack * rate_);
    env_ = 0.0f;
    striking_ = false;
    lp1_ = lp2_ = 0.0f;
    strike_edge_ = RisingEdge{};
  }

  void process(const float* ins, float* outs) override {
    if (strike_edge_(ins[2])) striking_ = true;

    // A strike opens toward 1 at least as fast as a linear kStrikeAttack ramp.
    const float target = std::max(static_cast<float>(striking_), std::clamp(ins[1], 0.0f, 1.0f));
    if (target > env_) {
      const float a_att = 1.0f - std::exp(-1.0f / (0.002f * rate_));
      const float rise = std::max((target - env_) * a_att, strike_step_ * striking_);
      env_ = std::min(env_ + rise, target);
    } else {
      if (decay_key_.changed({ins[4]})) {  // skip exp while decay holds
        const float tau = std::max(ins[4], 0.001f);
        a_dec_ = 1.0f - std::exp(-1.0f / (tau * rate_));
      }
      env_ = sanitize(env_ + (target - env_) * a_dec_);  // NaN cv/decay land here
    }
    striking_ = striking_ & (env_ < 1.0f);  // fully open: decay from here

    // A closed or held gate repeats (env_, damp): skip exp2/exp then.
    if (cutoff_key_.changed({env_, ins[3]})) {
      const float damp = std::clamp(ins[3], 0.0f, 1.0f);
      const float fc = std::min(30.0f * std::exp2(env_ * (9.0f - 6.0f * damp)),
                                0.45f * rate_);
      a_ = 1.0f - std::exp(-2.0f * std::numbers::pi_v<float> * fc / rate_);
    }
    const float a = a_;
    // Sanitized so a non-finite input or control cannot poison the state.
    lp1_ = sanitize(lp1_ + a * (ins[0] - lp1_));
    lp2_ = sanitize(lp2_ + a * (lp1_ - lp2_));
    outs[0] = lp2_ * env_;
  }

  void copy_state_from(const Module& other) override {
    const auto& o = static_cast<const Lpg&>(other);
    env_ = o.env_;
    lp1_ = o.lp1_;
    lp2_ = o.lp2_;
    strike_edge_ = o.strike_edge_;
    striking_ = o.striking_;
  }

 private:
  float rate_ = 48000.0f;
  float env_ = 0.0f;
  float strike_step_ = 0.0f;
  bool striking_ = false;
  float lp1_ = 0.0f, lp2_ = 0.0f;
  RisingEdge strike_edge_;
  InputKey<1> decay_key_;
  InputKey<2> cutoff_key_;
  float a_dec_ = 0.0f, a_ = 0.0f;
};

std::unique_ptr<Module> make() { return std::make_unique<Lpg>(); }
}  // namespace

const ModuleInfo lpg_info{"lpg", kIns, {}, kOuts, &make};
}  // namespace ms::dsp
