#include <algorithm>
#include <cmath>
#include <numbers>
#include "dsp/bandlimited_osc.h"
#include "dsp/input_key.h"
#include "dsp/modules.h"

namespace ms::dsp {
namespace {

constexpr InputInfo kIns[] = {{"freq", 220.f}, {"v_oct", 0.f}, {"fm", 0.f},
                              {"pw", 0.5f},    {"pwm", 0.f},   {"sync", 0.f},
                              {"phase", 0.f}};
constexpr const char* kWaves[] = {"sine", "tri", "saw", "pulse", nullptr};
constexpr ConfigInfo kCfg[] = {{"wave", kWaves}};
constexpr OutputInfo kOuts[] = {{"out"}};

class Vco final : public Module {
 public:
  void init(double rate, const Config& cfg) override {
    rate_ = static_cast<float>(rate);
    const std::string w = cfg_str(cfg, "wave", "sine");
    wave_ = w == "tri" ? 1 : w == "saw" ? 2 : w == "pulse" ? 3 : 0;
    phase_ = 0.0f;
    prev_sync_ = 0.0f;
    bank_ = &BandlimitedOsc::instance();
  }

  void process(const float* ins, float* outs) override {
    if (key_.changed({ins[0], ins[1], ins[2]})) {  // skip exp2 at a held pitch
      double f = static_cast<double>(ins[0]) * std::exp2(static_cast<double>(ins[1])) +
                 static_cast<double>(ins[2]) * static_cast<double>(ins[0]);
      if (!std::isfinite(f)) f = 0.0;
      f = std::clamp(f, 0.0, 0.45 * static_cast<double>(rate_));
      dt_ = f / static_cast<double>(rate_);
      band_ = bank_->band(static_cast<float>(f), rate_);
    }
    const double dt = dt_;

    const float sync = std::isfinite(ins[5]) ? ins[5] : 0.0f;
    if (sync > 0.5f && prev_sync_ <= 0.5f) phase_ = 0.0;
    prev_sync_ = sync;

    // Advance first (double phase): a k-Hz tone then completes exactly k cycles
    // over rate samples, and a sync reset lands the next sample near -1.
    phase_ += dt;
    phase_ -= std::floor(phase_);
    double offset = phase_ + (std::isfinite(ins[6]) ? static_cast<double>(ins[6]) : 0.0);
    offset -= std::floor(offset);
    const float ph = static_cast<float>(offset);

    float v = 0.0f;
    switch (wave_) {
      case 0:  // sine
        v = std::sin(2.0f * std::numbers::pi_v<float> * ph);
        break;
      case 1:  // band-limited triangle table
        v = bank_->triangle(ph, band_);
        break;
      case 2:  // band-limited saw table
        v = bank_->saw(ph, band_);
        break;
      case 3: {  // difference of two band-limited saws, plus exact DC term
        const float raw_pw = ins[3] + ins[4];
        const float pw = std::isfinite(raw_pw)
                             ? std::clamp(raw_pw, 0.05f, 0.95f)
                             : 0.5f;
        float shifted = ph - pw;
        shifted -= std::floor(shifted);
        v = bank_->saw(shifted, band_) - bank_->saw(ph, band_) +
            2.0f * pw - 1.0f;
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
  const BandlimitedOsc* bank_ = nullptr;
  InputKey<3> key_;  // freq, v_oct, fm -> dt_, band_
  double dt_ = 0.0;
  BandlimitedOsc::Band band_{};
};

std::unique_ptr<Module> make() { return std::make_unique<Vco>(); }
}  // namespace

const ModuleInfo vco_info{"vco", kIns, kCfg, kOuts, &make};
}  // namespace ms::dsp
