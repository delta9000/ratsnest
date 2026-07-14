#include <algorithm>
#include <cmath>
#include "dsp/modules.h"
#include "dsp/mastering.h"

namespace ms::dsp {
namespace {

constexpr InputInfo kIns[] = {
    {"in_l", 0.0f},
    {"in_r", 0.0f},
    {"threshold", -0.1f}, // dBFS
    {"release", 0.05f}    // s
};
constexpr OutputInfo kOuts[] = {
    {"out_l"},
    {"out_r"},
    {"gain"},
    {"gr"}
};

class Limiter final : public Module {
 public:
  void init(double rate, const Config& cfg) override {
    (void)cfg;
    rate_ = static_cast<float>(rate);
    
    g_smooth_ = 0.0f;
    
    // Clear ring buffers
    std::fill(std::begin(buffer_l_), std::end(buffer_l_), 0.0f);
    std::fill(std::begin(buffer_r_), std::end(buffer_r_), 0.0f);
    write_idx_ = 0;
  }

  void process(const float* ins, float* outs) override {
    // 1. Sanitize audio inputs and write to ring buffers
    const float in_l = sanitize(ins[0], 0.0f);
    const float in_r = sanitize(ins[1], 0.0f);
    
    buffer_l_[write_idx_] = in_l;
    buffer_r_[write_idx_] = in_r;
    
    // Sanitize and clamp control inputs
    const float threshold = sanitize_clamp(ins[2], -24.0f, 0.0f, -0.1f);
    const float release = sanitize_clamp(ins[3], 0.001f, 2.0f, 0.05f);
    
    // 2. Calculate delay size (2.0 ms lookahead)
    const int delay_samples = std::clamp(
        static_cast<int>(std::round(rate_ * 0.002f)),
        1,
        512
    );
    
    // 3. Scan the lookahead window in the ring buffer to find peak (including the read index sample)
    float max_peak = 1e-12f;
    for (int i = 0; i <= delay_samples; ++i) {
      int idx = (write_idx_ - i + 512) % 512;
      max_peak = std::max({max_peak, std::abs(buffer_l_[idx]), std::abs(buffer_r_[idx])});
    }
    
    // 4. Calculate gain reduction target in dB
    const float peak_db = 20.0f * std::log10(max_peak);
    float g_target = peak_db - threshold;
    if (g_target < 0.0f) g_target = 0.0f;
    
    // 5. Smooth gain reduction with fast attack (0.1 ms) and variable release
    const float alpha_a = 1.0f - std::exp(-1.0f / (0.0001f * rate_));
    const float alpha_r = 1.0f - std::exp(-1.0f / (release * rate_));
    
    if (g_target > g_smooth_) {
      g_smooth_ += alpha_a * (g_target - g_smooth_);
    } else {
      g_smooth_ += alpha_r * (g_target - g_smooth_);
    }
    g_smooth_ = flush_denormal(g_smooth_);
    
    // 6. Convert gain reduction to linear factor
    const float g_linear = std::pow(10.0f, -g_smooth_ / 20.0f);
    
    // 7. Process delayed audio from the ring buffer
    const int read_idx = (write_idx_ - delay_samples + 512) % 512;
    const float dry_l = buffer_l_[read_idx];
    const float dry_r = buffer_r_[read_idx];
    
    outs[0] = dry_l * g_linear;
    outs[1] = dry_r * g_linear;
    outs[2] = g_linear;
    outs[3] = g_smooth_;
    
    // Increment write index
    write_idx_ = (write_idx_ + 1) % 512;
  }

  void copy_state_from(const Module& other) override {
    const auto& o = static_cast<const Limiter&>(other);
    g_smooth_ = o.g_smooth_;
    std::copy(std::begin(o.buffer_l_), std::end(o.buffer_l_), std::begin(buffer_l_));
    std::copy(std::begin(o.buffer_r_), std::end(o.buffer_r_), std::begin(buffer_r_));
    write_idx_ = o.write_idx_;
  }

 private:
  float rate_ = 48000.0f;
  float g_smooth_ = 0.0f;
  
  // Ring buffers
  float buffer_l_[512] = {};
  float buffer_r_[512] = {};
  int write_idx_ = 0;
};

std::unique_ptr<Module> make() { return std::make_unique<Limiter>(); }

}  // namespace

const ModuleInfo limiter_info{"limiter", kIns, {}, kOuts, &make};

}  // namespace ms::dsp
