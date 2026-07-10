#include <algorithm>
#include <cmath>
#include <numbers>
#include "dsp/modules.h"

namespace ms::dsp {
namespace {

constexpr InputInfo kIns[] = {{"freq", 440.f}, {"v_oct", 0.f}, {"fm", 0.f},
                              {"pw", 0.5f},    {"pwm", 0.f},   {"sync", 0.f}};
constexpr const char* kWaves[] = {"sine", "tri", "saw", "pulse", nullptr};
constexpr ConfigInfo kCfg[] = {{"wave", kWaves}};
constexpr OutputInfo kOuts[] = {{"out"}};

// Standard 2-sample polyBLEP residual for a falling step at phase t.
float polyblep(float t, float dt) {
  if (t < dt) { t /= dt; return t + t - t * t - 1.0f; }
  if (t > 1.0f - dt) { t = (t - 1.0f) / dt; return t * t + t + t + 1.0f; }
  return 0.0f;
}

class Vco final : public Module {
 public:
  void init(double rate, const Config& cfg) override {
    rate_ = static_cast<float>(rate);
    const std::string w = cfg_str(cfg, "wave", "sine");
    wave_ = w == "tri" ? 1 : w == "saw" ? 2 : w == "pulse" ? 3 : 0;
    phase_ = 0.0f;
    prev_sync_ = 0.0f;
  }

  void process(const float* ins, float* outs) override {
    double f = static_cast<double>(ins[0]) * std::exp2(static_cast<double>(ins[1])) +
               static_cast<double>(ins[2]) * static_cast<double>(ins[0]);
    f = std::clamp(f, 0.0, 0.45 * static_cast<double>(rate_));
    const double dt = f / static_cast<double>(rate_);

    if (ins[5] > 0.5f && prev_sync_ <= 0.5f) phase_ = 0.0;
    prev_sync_ = ins[5];

    // Advance first (double phase): a k-Hz tone then completes exactly k cycles
    // over rate samples, and a sync reset lands the next sample near -1.
    phase_ += dt;
    phase_ -= std::floor(phase_);
    const float ph = static_cast<float>(phase_);
    const float dtf = static_cast<float>(dt);

    float v = 0.0f;
    switch (wave_) {
      case 0:  // sine
        v = std::sin(2.0f * std::numbers::pi_v<float> * ph);
        break;
      case 1:  // tri (naive; -12 dB/oct spectrum, acceptable aliasing)
        v = 4.0f * std::abs(ph - 0.5f) - 1.0f;
        break;
      case 2:  // saw, polyBLEP
        v = 2.0f * ph - 1.0f - polyblep(ph, dtf);
        break;
      case 3: {  // pulse, polyBLEP on both edges
        const float pw = std::clamp(ins[3] + ins[4], 0.05f, 0.95f);
        v = (ph < pw ? 1.0f : -1.0f);
        v += polyblep(ph, dtf);
        float t2 = ph + 1.0f - pw;
        t2 -= std::floor(t2);
        v -= polyblep(t2, dtf);
        break;
      }
    }
    outs[0] = v;
  }

  void copy_state_from(const Module& other) override {
    const auto& o = static_cast<const Vco&>(other);
    phase_ = o.phase_;
    prev_sync_ = o.prev_sync_;
  }

 private:
  float rate_ = 48000.0f;
  int wave_ = 0;
  double phase_ = 0.0;
  float prev_sync_ = 0.0f;
};

std::unique_ptr<Module> make() { return std::make_unique<Vco>(); }
}  // namespace

const ModuleInfo vco_info{"vco", kIns, kCfg, kOuts, &make};
}  // namespace ms::dsp
