// Mono phaser: a cascade of first-order allpass stages whose break frequency is
// swept by an internal LFO, with feedback around the cascade and a dry/wet sum.
// `spread` staggers the stage frequencies (0 = coincident, tight notches;
// 1 = fanned out, wider notches). Standard swept-allpass DSP; the allpass
// coefficient is the project's usual bilinear/TPT first-order allpass. See
// docs/attribution.md.
#include <array>
#include <cmath>
#include <memory>
#include <numbers>
#include <string>
#include "dsp/modules.h"

namespace ms::dsp {
namespace {

constexpr int kMaxStages = 12;
constexpr float kTwoPi = 2.0f * std::numbers::pi_v<float>;
constexpr float kPi = std::numbers::pi_v<float>;
constexpr float kFMin = 100.0f;
constexpr float kFMax = 4000.0f;

constexpr InputInfo kIns[] = {
    {"in", 0.f}, {"rate", 0.3f}, {"depth", 0.6f}, {"feedback", 0.3f}, {"mix", 0.5f}};
constexpr const char* kShapes[] = {"tri", "sine", nullptr};
constexpr ConfigInfo kCfg[] = {
    {"stages", nullptr}, {"spread", nullptr}, {"shape", kShapes}};
constexpr OutputInfo kOuts[] = {{"out"}};

float clampn(float x, float lo, float hi) {
  if (!(x >= lo)) return lo;
  return x > hi ? hi : x;
}
float lfo_shape(int shape, float ph) {  // ph in [0,1) -> [-1,1]
  if (shape == 1) return std::sin(kTwoPi * ph);
  return 1.0f - 4.0f * std::fabs(ph - 0.5f);  // triangle
}

class Phaser final : public Module {
 public:
  void init(double rate, const Config& cfg) override {
    rate_ = static_cast<float>(rate);
    stages_ = static_cast<int>(clampn(static_cast<float>(cfg_num(cfg, "stages", 6.0)),
                                      2.0f, static_cast<float>(kMaxStages)) + 0.5f);
    spread_ = clampn(static_cast<float>(cfg_num(cfg, "spread", 0.0)), 0.0f, 1.0f);
    shape_ = cfg_str(cfg, "shape", "tri") == "sine" ? 1 : 0;
    ap_.fill(0.0f);
    fb_ = 0.0f;
    phase_ = 0.0f;
  }

  void process(const float* ins, float* outs) override {
    const float in = ins[0];
    const float rate = clampn(ins[1], 0.0f, 100.0f);
    const float depth = clampn(ins[2], 0.0f, 1.0f);
    const float feedback = clampn(ins[3], 0.0f, 0.95f);
    const float mix = clampn(ins[4], 0.0f, 1.0f);

    const float m = lfo_shape(shape_, phase_);
    phase_ += rate / rate_;
    phase_ -= std::floor(phase_);

    const float nyq = 0.45f * rate_;
    const float fc = clampn(kFMin * std::pow(kFMax / kFMin, 0.5f * (depth * m + 1.0f)),
                            20.0f, nyq);
    const float centre = 0.5f * static_cast<float>(stages_ - 1);

    float x = in + feedback * fb_;
    for (int k = 0; k < stages_; ++k) {
      const float fck = clampn(
          fc * std::exp2(spread_ * (static_cast<float>(k) - centre) /
                         static_cast<float>(stages_)),
          20.0f, nyq);
      const float t = std::tan(kPi * fck / rate_);
      const float a = (t - 1.0f) / (t + 1.0f);
      const float y = a * x + ap_[static_cast<size_t>(k)];
      ap_[static_cast<size_t>(k)] = sanitize(x - a * y);
      if (std::fabs(ap_[static_cast<size_t>(k)]) < 1e-20f) ap_[static_cast<size_t>(k)] = 0.0f;
      x = y;
    }
    fb_ = sanitize(x);  // a non-finite input must not poison the loop
    if (std::fabs(fb_) < 1e-20f) fb_ = 0.0f;

    outs[0] = (1.0f - mix) * in + mix * x;
  }

  void copy_state_from(const Module& other) override {
    const auto& o = static_cast<const Phaser&>(other);
    ap_ = o.ap_;
    fb_ = o.fb_;
    phase_ = o.phase_;
  }

 private:
  float rate_ = 48000.0f;
  int stages_ = 6;
  int shape_ = 0;
  float spread_ = 0.0f;
  std::array<float, kMaxStages> ap_{};
  float fb_ = 0.0f;
  float phase_ = 0.0f;
};

std::unique_ptr<Module> make() { return std::make_unique<Phaser>(); }
}  // namespace

const ModuleInfo phaser_info{"phaser", kIns, kCfg, kOuts, &make};
}  // namespace ms::dsp
