#include <algorithm>
#include <cmath>
#include <numbers>
#include "dsp/modules.h"
#include "dsp/mastering.h"

namespace ms::dsp {
namespace {

constexpr InputInfo kIns[] = {
    {"in_l", 0.f},
    {"in_r", 0.f},
    {"width", 1.f},
    {"balance", 0.f},
    {"mono", 0.f},
    {"swap", 0.f}
};
constexpr OutputInfo kOuts[] = {
    {"out_l"},
    {"out_r"},
    {"mid"},
    {"side"},
    {"corr"}
};

class Stereo final : public Module {
 public:
  void init(double rate, const Config& cfg) override {
    (void)cfg;
    rate_ = static_cast<float>(rate);
    
    // Width and balance smoothed around 20 ms
    width_smooth_.set_time(0.020f, rate_);
    width_smooth_.init(1.0f);
    
    balance_smooth_.set_time(0.020f, rate_);
    balance_smooth_.init(0.0f);
    
    // Correlation filter coefficient for 300 ms window
    corr_coeff_ = 1.0f - std::exp(-1.0f / (0.300f * rate_));
    
    // Initialize states
    s_xx_ = 0.0f;
    s_yy_ = 0.0f;
    s_xy_ = 0.0f;
  }

  void process(const float* ins, float* outs) override {
    // Sanitize parameters
    const float raw_in_l = sanitize(ins[0], 0.0f);
    const float raw_in_r = sanitize(ins[1], 0.0f);
    const float raw_width = sanitize_clamp(ins[2], 0.0f, 2.0f, 1.0f);
    const float raw_balance = sanitize_clamp(ins[3], -1.0f, 1.0f, 0.0f);
    const float raw_mono = sanitize(ins[4], 0.0f);
    const float raw_swap = sanitize(ins[5], 0.0f);
    
    // Smooth width and balance
    const float width = width_smooth_.process(raw_width);
    const float balance = balance_smooth_.process(raw_balance);
    const bool is_mono = raw_mono > 0.5f;
    const bool is_swap = raw_swap > 0.5f;
    
    // 1. M/S encode
    const float m_in = 0.5f * (raw_in_l + raw_in_r);
    const float s_in = 0.5f * (raw_in_l - raw_in_r);
    
    // 2. M/S width adjustments
    const float coef_m = (width > 1.0f) ? (2.0f - width) : 1.0f;
    const float coef_s = width;
    const float m_width = coef_m * m_in;
    const float s_width = coef_s * s_in;
    
    // 3. M/S decode
    float l_val = m_width + s_width;
    float r_val = m_width - s_width;
    
    // 4. Equal-power balance
    const float angle = (balance + 1.0f) * (std::numbers::pi_v<float> / 4.0f);
    const float gain_l = std::cos(angle);
    const float gain_r = std::sin(angle);
    
    float l_bal = l_val * gain_l;
    float r_bal = r_val * gain_r;
    
    // 5. Level-sensed mono gate
    if (is_mono) {
      const float mono_sum = 0.5f * (l_bal + r_bal);
      l_bal = mono_sum;
      r_bal = mono_sum;
    }
    
    // 6. Level-sensed swap gate
    if (is_swap) {
      std::swap(l_bal, r_bal);
    }
    
    // Write out outputs
    outs[0] = l_bal;
    outs[1] = r_bal;
    outs[2] = 0.5f * (l_bal + r_bal);
    outs[3] = 0.5f * (l_bal - r_bal);
    
    // 7. 300 ms exponential correlation accumulation
    s_xx_ += corr_coeff_ * (l_bal * l_bal - s_xx_);
    s_yy_ += corr_coeff_ * (r_bal * r_bal - s_yy_);
    s_xy_ += corr_coeff_ * (l_bal * r_bal - s_xy_);
    
    s_xx_ = flush_denormal(s_xx_);
    s_yy_ = flush_denormal(s_yy_);
    s_xy_ = flush_denormal(s_xy_);
    
    float corr = 0.0f;
    const float denom = std::sqrt(s_xx_ * s_yy_);
    if (denom > 1e-12f) {
      corr = std::clamp(s_xy_ / denom, -1.0f, 1.0f);
    }
    outs[4] = corr;
  }

  void copy_state_from(const Module& other) override {
    const auto& o = static_cast<const Stereo&>(other);
    width_smooth_ = o.width_smooth_;
    balance_smooth_ = o.balance_smooth_;
    s_xx_ = o.s_xx_;
    s_yy_ = o.s_yy_;
    s_xy_ = o.s_xy_;
  }

 private:
  float rate_ = 48000.0f;
  float corr_coeff_ = 0.0f;
  OnePole width_smooth_;
  OnePole balance_smooth_;
  float s_xx_ = 0.0f;
  float s_yy_ = 0.0f;
  float s_xy_ = 0.0f;
};

std::unique_ptr<Module> make() { return std::make_unique<Stereo>(); }

}  // namespace

const ModuleInfo stereo_info{"stereo", kIns, {}, kOuts, &make};

}  // namespace ms::dsp
