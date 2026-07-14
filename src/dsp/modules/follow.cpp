#include <cmath>
#include "dsp/modules.h"
#include "dsp/mastering.h"

namespace ms::dsp {
namespace {

constexpr InputInfo kIns[] = {
    {"in", 0.0f},
    {"attack", 0.01f},   // s
    {"release", 0.1f},   // s
    {"thresh", 0.1f},    // unipolar gate threshold
};
constexpr const char* kModes[] = {"peak", "rms", nullptr};
constexpr ConfigInfo kCfg[] = {
    {"mode", kModes},
};
constexpr OutputInfo kOuts[] = {
    {"env"},
    {"gate"},
};

class Follow final : public Module {
 public:
  void init(double rate, const Config& cfg) override {
    rate_ = static_cast<float>(rate);
    is_rms_ = (cfg_str(cfg, "mode", "peak") == "rms");
    state_ = 0.0f;
    gate_high_ = false;
  }

  void process(const float* ins, float* outs) override {
    const float in = sanitize(ins[0], 0.0f);
    const float attack = sanitize_clamp(ins[1], 0.0001f, 10.0f, 0.01f);
    const float release = sanitize_clamp(ins[2], 0.0001f, 10.0f, 0.1f);
    const float thresh = sanitize_clamp(ins[3], 0.0f, 8.0f, 0.1f);

    // 1. Rectify by mode: peak follows |in|, rms follows in^2 (energy).
    const float x = is_rms_ ? in * in : std::abs(in);

    // 2. One-pole ballistics with asymmetric attack/release coefficients.
    const float alpha_a = 1.0f - std::exp(-1.0f / (attack * rate_));
    const float alpha_r = 1.0f - std::exp(-1.0f / (release * rate_));
    state_ += (x > state_ ? alpha_a : alpha_r) * (x - state_);
    state_ = flush_denormal(state_);

    // 3. Envelope: undo the squaring in rms mode. Linear amplitude, >= 0,
    //    intentionally unclamped on top so hot signals read honestly.
    const float env = is_rms_ ? std::sqrt(state_) : state_;

    // 4. Gate with fixed relative hysteresis: on at thresh, off at 75% of it.
    if (env > thresh) {
      gate_high_ = true;
    } else if (env < 0.75f * thresh) {
      gate_high_ = false;
    }

    outs[0] = env;
    outs[1] = gate_high_ ? 1.0f : 0.0f;
  }

 private:
  float rate_ = 48000.0f;
  bool is_rms_ = false;
  float state_ = 0.0f;
  bool gate_high_ = false;
};

std::unique_ptr<Module> make() { return std::make_unique<Follow>(); }

}  // namespace

const ModuleInfo follow_info{"follow", kIns, kCfg, kOuts, &make};

}  // namespace ms::dsp
