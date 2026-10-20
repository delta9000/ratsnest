#include <algorithm>
#include <bit>
#include <cstdint>
#include <cmath>
#include <numbers>
#include "dsp/input_key.h"
#include "dsp/modules.h"

// Paper/technical-note-derived Zavalishin topology-preserving four-stage
// ladder with analytically solved linear feedback. See docs/attribution.md.

namespace ms::dsp {
namespace {

constexpr InputInfo kIns[] = {
    {"in", 0.f}, {"cutoff", 1000.f}, {"res", 0.f}, {"drive", 0.f}, {"v_oct", 0.f}};
constexpr OutputInfo kOuts[] = {{"out"}};

class Ladder final : public Module {
 public:
  void init(double rate, const Config&) override {
    rate_ = static_cast<float>(rate);
    s1_ = s2_ = s3_ = s4_ = 0.0f;
  }

  void process(const float* ins, float* outs) override {
    if (key_.changed({ins[1], ins[4]})) {  // skip exp2/tan when still
      const double oct = std::isfinite(ins[4]) ? std::clamp(static_cast<double>(ins[4]), -24.0, 24.0) : 0.0;
      const float cutoff = static_cast<float>(std::clamp(
          static_cast<double>(ins[1]) * std::exp2(oct), 5.0, 0.45 * rate_));
      const float g = std::tan(std::numbers::pi_v<float> * cutoff / rate_);
      G_ = g / (1.0f + g);
    }
    const float G = G_;
    const float res = std::clamp(ins[2], 0.0f, 1.0f);
    const float drive = std::clamp(ins[3], 0.0f, 4.0f);
    const float k = 4.0f * res;
    // Drive is a bypass at its default (0): blend linearly between the clean
    // input and the driven tanh(in*(1+4*drive)) saturation so drive==0 is an
    // exact identity (per-sample defaults must be neutral), and full drive
    // reaches the fully saturated stage.
    // At drive == +0 and a finite input the blend term is a signed zero
    // whose sign works out to +0 either way, so skip the tanh.
    const float x = std::bit_cast<std::uint32_t>(drive) == 0u && std::isfinite(ins[0])
        ? ins[0] + 0.0f
        : ins[0] + 0.25f * drive * (std::tanh(ins[0] * (1.0f + 4.0f * drive)) - ins[0]);

    // TPT one-pole per stage: y = G*x + (1-G)*s.  Zero-delay feedback:
    // y4 = G^4*u + S with u = x - k*y4, solved for y4 before updating states.
    const float G2 = G * G;
    const float zf = 1.0f - G;
    const float S = G2 * G * zf * s1_ + G2 * zf * s2_ + G * zf * s3_ + zf * s4_;
    const float y4 = (G2 * G2 * x + S) / (1.0f + k * G2 * G2);
    const float u = x - k * y4;

    const float y1 = G * u + zf * s1_;
    s1_ = 2.0f * y1 - s1_;
    const float y2 = G * y1 + zf * s2_;
    s2_ = 2.0f * y2 - s2_;
    const float y3 = G * y2 + zf * s3_;
    s3_ = 2.0f * y3 - s3_;
    const float y4b = G * y3 + zf * s4_;
    s4_ = 2.0f * y4b - s4_;

    outs[0] = y4b;
  }

  void copy_state_from(const Module& other) override {
    const auto& o = static_cast<const Ladder&>(other);
    s1_ = o.s1_;
    s2_ = o.s2_;
    s3_ = o.s3_;
    s4_ = o.s4_;
  }

 private:
  float rate_ = 48000.0f;
  float s1_ = 0.0f, s2_ = 0.0f, s3_ = 0.0f, s4_ = 0.0f;
  InputKey<2> key_;  // cutoff, v_oct -> G_
  float G_ = 0.0f;
};

std::unique_ptr<Module> make() { return std::make_unique<Ladder>(); }
}  // namespace

const ModuleInfo ladder_info{"ladder", kIns, {}, kOuts, &make};
}  // namespace ms::dsp
