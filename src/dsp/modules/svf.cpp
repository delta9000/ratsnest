#include <algorithm>
#include <cmath>
#include <numbers>
#include "dsp/modules.h"

namespace ms::dsp {
namespace {

constexpr InputInfo kIns[] = {{"in", 0.f}, {"cutoff", 1000.f}, {"res", 0.f}};
constexpr OutputInfo kOuts[] = {{"lp"}, {"bp"}, {"hp"}};

class SvfModule final : public Module {
 public:
  void init(double rate, const Config&) override {
    rate_ = static_cast<float>(rate);
    ic1eq_ = 0.0f;
    ic2eq_ = 0.0f;
  }

  void process(const float* ins, float* outs) override {
    const float cutoff = std::clamp(ins[1], 5.0f, 0.45f * rate_);
    const float resonance = std::clamp(ins[2], 0.0f, 1.0f);
    const float g = std::tan(std::numbers::pi_v<float> * cutoff / rate_);
    const float k = 2.0f * (1.0f - resonance) + 0.02f * resonance;
    const float a1 = 1.0f / (1.0f + g * (g + k));
    const float a2 = g * a1;
    const float a3 = g * a2;

    const float v3 = ins[0] - ic2eq_;
    const float bp = a1 * ic1eq_ + a2 * v3;
    const float lp = ic2eq_ + a2 * ic1eq_ + a3 * v3;
    const float hp = ins[0] - k * bp - lp;

    ic1eq_ = 2.0f * bp - ic1eq_;
    ic2eq_ = 2.0f * lp - ic2eq_;
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
};

std::unique_ptr<Module> make() { return std::make_unique<SvfModule>(); }
}  // namespace

const ModuleInfo svf_info{"svf", kIns, {}, kOuts, &make};
}  // namespace ms::dsp
