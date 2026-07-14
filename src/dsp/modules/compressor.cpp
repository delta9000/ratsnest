#include <algorithm>
#include <cmath>
#include <numbers>
#include "dsp/modules.h"
#include "dsp/mastering.h"

namespace ms::dsp {
namespace {

constexpr InputInfo kIns[] = {
    {"in_l", 0.0f},
    {"in_r", 0.0f},
    {"side_l", 0.0f},
    {"side_r", 0.0f},
    {"threshold", -18.0f}, // dBFS
    {"ratio", 4.0f},
    {"knee", 6.0f},        // dB
    {"attack", 0.01f},     // s
    {"release", 0.1f},     // s
    {"makeup", 0.0f},      // dB
    {"mix", 1.0f}          // 0..1
};
constexpr const char* kSidechains[] = {"internal", "external", nullptr};
constexpr ConfigInfo kCfg[] = {
    {"sidechain", kSidechains}
};
constexpr OutputInfo kOuts[] = {
    {"out_l"},
    {"out_r"},
    {"gain"},
    {"gr"}
};

class Compressor final : public Module {
 public:
  void init(double rate, const Config& cfg) override {
    rate_ = static_cast<float>(rate);
    const std::string sc = cfg_str(cfg, "sidechain", "internal");
    is_external_ = (sc == "external");
    
    g_smooth_ = 0.0f;
  }

  void process(const float* ins, float* outs) override {
    // Sanitize audio inputs
    const float in_l = sanitize(ins[0], 0.0f);
    const float in_r = sanitize(ins[1], 0.0f);
    const float side_l = sanitize(ins[2], 0.0f);
    const float side_r = sanitize(ins[3], 0.0f);
    
    // Sanitize and clamp control inputs
    const float threshold = sanitize_clamp(ins[4], -120.0f, 0.0f, -18.0f);
    const float ratio = sanitize_clamp(ins[5], 1.0f, 1000.0f, 4.0f);
    const float knee = sanitize_clamp(ins[6], 0.0f, 40.0f, 6.0f);
    const float attack = sanitize_clamp(ins[7], 0.0001f, 10.0f, 0.01f);
    const float release = sanitize_clamp(ins[8], 0.0001f, 10.0f, 0.1f);
    const float makeup = sanitize_clamp(ins[9], -40.0f, 40.0f, 0.0f);
    const float mix = sanitize_clamp(ins[10], 0.0f, 1.0f, 1.0f);
    
    // 1. Level detection (stereo-linked max-peak)
    float det_l = is_external_ ? side_l : in_l;
    float det_r = is_external_ ? side_r : in_r;
    float level_linear = std::max({std::abs(det_l), std::abs(det_r), 1e-12f});
    float level_db = 20.0f * std::log10(level_linear);
    
    // 2. Giannoulis log-domain curve
    float y_db = level_db;
    if (knee > 0.0f) {
      if (2.0f * (level_db - threshold) < -knee) {
        y_db = level_db;
      } else if (2.0f * std::abs(level_db - threshold) <= knee) {
        float knee_val = level_db - threshold + 0.5f * knee;
        y_db = level_db + ((1.0f / ratio - 1.0f) * knee_val * knee_val) / (2.0f * knee);
      } else {
        y_db = threshold + (level_db - threshold) / ratio;
      }
    } else {
      if (level_db > threshold) {
        y_db = threshold + (level_db - threshold) / ratio;
      }
    }
    
    float g_target = level_db - y_db;
    if (g_target < 0.0f) g_target = 0.0f;
    
    // 3. Separate smoothed attack/release
    const float alpha_a = 1.0f - std::exp(-1.0f / (attack * rate_));
    const float alpha_r = 1.0f - std::exp(-1.0f / (release * rate_));
    
    if (g_target > g_smooth_) {
      g_smooth_ += alpha_a * (g_target - g_smooth_);
    } else {
      g_smooth_ += alpha_r * (g_target - g_smooth_);
    }
    g_smooth_ = flush_denormal(g_smooth_);
    
    // 4. Convert back to linear gain reduction
    const float g_linear = std::pow(10.0f, -g_smooth_ / 20.0f);
    
    // 5. Apply makeup and crossfade mix
    const float makeup_linear = std::pow(10.0f, makeup / 20.0f);
    const float wet_gain = g_linear * makeup_linear;
    
    const float wet_l = in_l * wet_gain;
    const float wet_r = in_r * wet_gain;
    
    const float mix_angle = mix * (std::numbers::pi_v<float> / 2.0f);
    const float gain_dry = std::cos(mix_angle);
    const float gain_wet = std::sin(mix_angle);
    
    outs[0] = gain_dry * in_l + gain_wet * wet_l;
    outs[1] = gain_dry * in_r + gain_wet * wet_r;
    outs[2] = g_linear;
    outs[3] = g_smooth_;
  }

  void copy_state_from(const Module& other) override {
    const auto& o = static_cast<const Compressor&>(other);
    g_smooth_ = o.g_smooth_;
  }

 private:
  float rate_ = 48000.0f;
  bool is_external_ = false;
  float g_smooth_ = 0.0f;
};

std::unique_ptr<Module> make() { return std::make_unique<Compressor>(); }

}  // namespace

const ModuleInfo compressor_info{"compressor", kIns, kCfg, kOuts, &make};

}  // namespace ms::dsp
