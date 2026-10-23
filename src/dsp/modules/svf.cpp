#include <algorithm>
#include <cmath>
#include <numbers>
#include "dsp/input_key.h"
#include "dsp/modules.h"

// Paper/technical-note-derived Zavalishin/Simper trapezoidal-integrator TPT
// state-variable filter, with amplitude-dependent (van der Pol-style) damping
// so the top of the res range self-oscillates into a bounded sine.
// See docs/attribution.md.

namespace ms::dsp {
namespace {

constexpr InputInfo kIns[] = {{"in", 0.f}, {"cutoff", 1000.f}, {"res", 0.f}, {"v_oct", 0.f}};
constexpr OutputInfo kOuts[] = {{"lp"}, {"bp"}, {"hp"}};

// Damping k at res 1 (negative: the filter self-oscillates there) and the
// cubic damping coefficient that bounds it: a band-pass oscillation of
// amplitude A settles where k + 0.75*kCubic*res*A^2 = 0, so A ~ 1 at res 1.
constexpr float kTopDamping = -0.1f;
constexpr float kCubic = 0.133f;

class SvfModule final : public Module {
 public:
  void init(double rate, const Config&) override {
    rate_ = static_cast<float>(rate);
    ic1eq_ = 0.0f;
    ic2eq_ = 0.0f;
    bp_ = 0.0f;
  }

  void process(const float* ins, float* outs) override {
    if (key_.changed({ins[1], ins[2], ins[3]})) {  // skip exp2/tan when still
      const double oct = std::isfinite(ins[3]) ? std::clamp(static_cast<double>(ins[3]), -24.0, 24.0) : 0.0;
      const float cutoff = static_cast<float>(std::clamp(
          static_cast<double>(ins[1]) * std::exp2(oct), 5.0, 0.45 * rate_));
      const float resonance = std::clamp(ins[2], 0.0f, 1.0f);
      const float g = std::tan(std::numbers::pi_v<float> * cutoff / rate_);
      const float r4 = resonance * resonance * resonance * resonance;
      // The old Q 0.5..~5 curve (within 10%) up to res 0.9; it bends below
      // zero damping near 1.
      k_ = 2.0f * (1.0f - resonance) + 0.02f * resonance - (0.02f - kTopDamping) * r4 * r4 * r4 * r4;
      cubic_ = kCubic * resonance;
      g_ = g;
    }
    // Damping k + cubic*bp^2 grows with the band-pass level. The implicit
    // equation is solved by two fixed-point passes (seeded with the previous
    // sample's bp), each a closed-form linear TPT solve; at res 0 the cubic
    // term is zero and the filter is exactly linear.
    const float g = g_, v3 = ins[0] - ic2eq_;
    float k = k_, a2 = 0.0f, bp = bp_;
    for (int pass = 0; pass < 2; ++pass) {
      k = k_ + cubic_ * bp * bp;
      const float a1 = 1.0f / (1.0f + g * (g + k));
      a2 = g * a1;
      bp = a1 * ic1eq_ + a2 * v3;
    }
    const float lp = ic2eq_ + a2 * ic1eq_ + g * a2 * v3;
    const float hp = ins[0] - k * bp - lp;

    // Sanitized so a non-finite input or control cannot poison the filter.
    ic1eq_ = sanitize(2.0f * bp - ic1eq_);
    ic2eq_ = sanitize(2.0f * lp - ic2eq_);
    bp_ = sanitize(bp);
    outs[0] = lp;
    outs[1] = bp;
    outs[2] = hp;
  }

  void copy_state_from(const Module& other) override {
    const auto& o = static_cast<const SvfModule&>(other);
    ic1eq_ = o.ic1eq_;
    ic2eq_ = o.ic2eq_;
    bp_ = o.bp_;
  }

 private:
  float rate_ = 48000.0f;
  float ic1eq_ = 0.0f;
  float ic2eq_ = 0.0f;
  float bp_ = 0.0f;  // previous band-pass sample, drives the cubic damping
  InputKey<3> key_;  // cutoff, res, v_oct -> k_, cubic_, g_
  float k_ = 0.0f, cubic_ = 0.0f, g_ = 0.0f;
};

std::unique_ptr<Module> make() { return std::make_unique<SvfModule>(); }
}  // namespace

const ModuleInfo svf_info{"svf", kIns, {}, kOuts, &make};
}  // namespace ms::dsp
