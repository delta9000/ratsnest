#include <algorithm>
#include <cmath>
#include <numbers>
#include "dsp/input_key.h"
#include "dsp/modules.h"

// Paper/technical-note-derived Zavalishin/Simper trapezoidal-integrator TPT
// state-variable filter. See docs/attribution.md.

namespace ms::dsp {
namespace {

constexpr InputInfo kIns[] = {{"in", 0.f}, {"cutoff", 1000.f}, {"res", 0.f}, {"v_oct", 0.f}};
constexpr OutputInfo kOuts[] = {{"lp"}, {"bp"}, {"hp"}};

class SvfModule final : public Module {
 public:
  void init(double rate, const Config&) override {
    rate_ = static_cast<float>(rate);
    ic1eq_ = 0.0f;
    ic2eq_ = 0.0f;
  }

  void process(const float* ins, float* outs) override {
    if (key_.changed({ins[1], ins[2], ins[3]})) {  // skip exp2/tan when still
      const double oct = std::isfinite(ins[3]) ? std::clamp(static_cast<double>(ins[3]), -24.0, 24.0) : 0.0;
      const float cutoff = static_cast<float>(std::clamp(
          static_cast<double>(ins[1]) * std::exp2(oct), 5.0, 0.45 * rate_));
      const float resonance = std::clamp(ins[2], 0.0f, 1.0f);
      const float g = std::tan(std::numbers::pi_v<float> * cutoff / rate_);
      k_ = 2.0f * (1.0f - resonance) + 0.02f * resonance;
      a1_ = 1.0f / (1.0f + g * (g + k_));
      a2_ = g * a1_;
      a3_ = g * a2_;
    }
    const float k = k_, a1 = a1_, a2 = a2_, a3 = a3_;

    const float v3 = ins[0] - ic2eq_;
    const float bp = a1 * ic1eq_ + a2 * v3;
    const float lp = ic2eq_ + a2 * ic1eq_ + a3 * v3;
    const float hp = ins[0] - k * bp - lp;

    // Sanitized so a non-finite input or control cannot poison the filter.
    ic1eq_ = sanitize(2.0f * bp - ic1eq_);
    ic2eq_ = sanitize(2.0f * lp - ic2eq_);
    outs[0] = lp;
    outs[1] = bp;
    outs[2] = hp;
  }

  void copy_state_from(const Module& other) override {
    const auto& o = static_cast<const SvfModule&>(other);
    ic1eq_ = o.ic1eq_;
    ic2eq_ = o.ic2eq_;
  }

 private:
  float rate_ = 48000.0f;
  float ic1eq_ = 0.0f;
  float ic2eq_ = 0.0f;
  InputKey<3> key_;  // cutoff, res, v_oct -> k_, a1_..a3_
  float k_ = 0.0f, a1_ = 0.0f, a2_ = 0.0f, a3_ = 0.0f;
};

std::unique_ptr<Module> make() { return std::make_unique<SvfModule>(); }
}  // namespace

const ModuleInfo svf_info{"svf", kIns, {}, kOuts, &make};
}  // namespace ms::dsp
