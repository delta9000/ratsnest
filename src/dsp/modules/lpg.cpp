#include <algorithm>
#include <cmath>
#include <numbers>
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

class Lpg final : public Module {
 public:
  void init(double rate, const Config&) override {
    rate_ = static_cast<float>(rate);
    env_ = 0.0f;
    lp1_ = lp2_ = 0.0f;
    strike_edge_ = RisingEdge{};
  }

  void process(const float* ins, float* outs) override {
    if (strike_edge_(ins[2])) env_ = 1.0f;

    const float target = std::clamp(ins[1], 0.0f, 1.0f);
    if (target > env_) {
      const float a_att = 1.0f - std::exp(-1.0f / (0.002f * rate_));
      env_ += (target - env_) * a_att;
    } else {
      const float tau = std::max(ins[4], 0.001f);
      const float a_dec = 1.0f - std::exp(-1.0f / (tau * rate_));
      env_ += (target - env_) * a_dec;
    }

    const float damp = std::clamp(ins[3], 0.0f, 1.0f);
    const float fc = std::min(30.0f * std::exp2(env_ * (9.0f - 6.0f * damp)),
                              0.45f * rate_);
    const float a =
        1.0f - std::exp(-2.0f * std::numbers::pi_v<float> * fc / rate_);
    lp1_ += a * (ins[0] - lp1_);
    lp2_ += a * (lp1_ - lp2_);
    outs[0] = lp2_ * env_;
  }

  void copy_state_from(const Module& other) override {
    const auto& o = static_cast<const Lpg&>(other);
    env_ = o.env_;
    lp1_ = o.lp1_;
    lp2_ = o.lp2_;
    strike_edge_ = o.strike_edge_;
  }

 private:
  float rate_ = 48000.0f;
  float env_ = 0.0f;
  float lp1_ = 0.0f, lp2_ = 0.0f;
  RisingEdge strike_edge_;
};

std::unique_ptr<Module> make() { return std::make_unique<Lpg>(); }
}  // namespace

const ModuleInfo lpg_info{"lpg", kIns, {}, kOuts, &make};
}  // namespace ms::dsp
