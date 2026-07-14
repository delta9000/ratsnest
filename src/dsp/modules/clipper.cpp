#include <algorithm>
#include <cmath>
#include <numbers>
#include "dsp/modules.h"
#include "dsp/oversample.h"
#include "dsp/mastering.h"

namespace ms::dsp {
namespace {

constexpr InputInfo kIns[] = {
    {"in_l", 0.0f},
    {"in_r", 0.0f},
    {"threshold", -1.0f}, // dBFS
    {"drive", 0.0f},      // dB
    {"mix", 1.0f}         // 0..1
};
constexpr const char* kModes[] = {"hard", "soft", nullptr};
constexpr ConfigInfo kCfg[] = {
    {"mode", kModes}
};
constexpr OutputInfo kOuts[] = {
    {"out_l"},
    {"out_r"}
};

// Polyphase IIR 4x oversampler has an impulse response peak at exactly 1 sample delay
constexpr int kLatency = 1;

class Clipper final : public Module {
 public:
  void init(double rate, const Config& cfg) override {
    rate_ = static_cast<float>(rate);
    const std::string m = cfg_str(cfg, "mode", "hard");
    is_soft_ = (m == "soft");
    
    oversampler_l_.clear();
    oversampler_r_.clear();
    decimator_l_.clear();
    decimator_r_.clear();
    
    // Clear dry delay line
    std::fill(std::begin(delay_l_), std::end(delay_l_), 0.0f);
    std::fill(std::begin(delay_r_), std::end(delay_r_), 0.0f);
    delay_idx_ = 0;
  }

  void process(const float* ins, float* outs) override {
    // Sanitize audio inputs
    const float in_l = sanitize(ins[0], 0.0f);
    const float in_r = sanitize(ins[1], 0.0f);
    
    // Sanitize and clamp control inputs
    const float threshold = sanitize_clamp(ins[2], -120.0f, 0.0f, -1.0f);
    const float drive = sanitize_clamp(ins[3], -40.0f, 40.0f, 0.0f);
    const float mix = sanitize_clamp(ins[4], 0.0f, 1.0f, 1.0f);
    
    // Write to dry delay line
    delay_l_[delay_idx_] = in_l;
    delay_r_[delay_idx_] = in_r;
    
    const float dry_l = delay_l_[(delay_idx_ + 8 - kLatency) % 8];
    const float dry_r = delay_r_[(delay_idx_ + 8 - kLatency) % 8];
    
    delay_idx_ = (delay_idx_ + 1) % 8;
    
    // 1. Convert DB control parameters to linear
    const float threshold_linear = std::pow(10.0f, threshold / 20.0f);
    const float drive_linear = std::pow(10.0f, drive / 20.0f);
    
    // 2. Upsample to 4x
    float xs_l[4] = {};
    float xs_r[4] = {};
    oversampler_l_.upsample(in_l, xs_l);
    oversampler_r_.upsample(in_r, xs_r);
    
    // 3. Apply gain drive & clip at 4x
    float ys_l[4] = {};
    float ys_r[4] = {};
    const float soft_denom = std::max(threshold_linear, 1e-9f);
    
    for (int i = 0; i < 4; ++i) {
      const float drive_l = xs_l[i] * drive_linear;
      const float drive_r = xs_r[i] * drive_linear;
      
      if (is_soft_) {
        // y = T * tanh(x / T)
        ys_l[i] = threshold_linear * std::tanh(drive_l / soft_denom);
        ys_r[i] = threshold_linear * std::tanh(drive_r / soft_denom);
      } else {
        // y = clamp(x, -T, T)
        ys_l[i] = std::clamp(drive_l, -threshold_linear, threshold_linear);
        ys_r[i] = std::clamp(drive_r, -threshold_linear, threshold_linear);
      }
    }
    
    // 4. Downsample back to 1x
    const float wet_l = decimator_l_.downsample(ys_l);
    const float wet_r = decimator_r_.downsample(ys_r);
    
    // 5. Equal-power dry/wet mix
    const float mix_angle = mix * (std::numbers::pi_v<float> / 2.0f);
    const float gain_dry = std::cos(mix_angle);
    const float gain_wet = std::sin(mix_angle);
    
    outs[0] = gain_dry * dry_l + gain_wet * wet_l;
    outs[1] = gain_dry * dry_r + gain_wet * wet_r;
  }

  void copy_state_from(const Module& other) override {
    const auto& o = static_cast<const Clipper&>(other);
    
    // Copy IIR oversampling states
    oversampler_l_ = o.oversampler_l_;
    oversampler_r_ = o.oversampler_r_;
    decimator_l_ = o.decimator_l_;
    decimator_r_ = o.decimator_r_;
    
    // Copy delay line state
    std::copy(std::begin(o.delay_l_), std::end(o.delay_l_), std::begin(delay_l_));
    std::copy(std::begin(o.delay_r_), std::end(o.delay_r_), std::begin(delay_r_));
    delay_idx_ = o.delay_idx_;
  }

 private:
  float rate_ = 48000.0f;
  bool is_soft_ = false;
  
  // Left oversampler/decimator
  Oversampler4x oversampler_l_;
  Decimator4x decimator_l_;
  
  // Right oversampler/decimator
  Oversampler4x oversampler_r_;
  Decimator4x decimator_r_;
  
  // Latency delay line (size 8 buffer is plenty for 1 sample latency)
  float delay_l_[8] = {};
  float delay_r_[8] = {};
  int delay_idx_ = 0;
};

std::unique_ptr<Module> make() { return std::make_unique<Clipper>(); }

}  // namespace

const ModuleInfo clipper_info{"clipper", kIns, kCfg, kOuts, &make};

}  // namespace ms::dsp
