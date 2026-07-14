#include <algorithm>
#include <cmath>
#include "dsp/modules.h"
#include "dsp/biquad.h"
#include "dsp/mastering.h"

namespace ms::dsp {
namespace {

constexpr InputInfo kIns[] = {
    {"in_l", 0.0f},
    {"in_r", 0.0f},
    {"hp_freq", 20.0f},
    {"low_freq", 120.0f},
    {"low_gain", 0.0f},       // dB
    {"mid1_freq", 500.0f},
    {"mid1_gain", 0.0f},      // dB
    {"mid1_q", 0.707f},
    {"mid2_freq", 2500.0f},
    {"mid2_gain", 0.0f},      // dB
    {"mid2_q", 0.707f},
    {"high_freq", 10000.0f},
    {"high_gain", 0.0f}       // dB
};
constexpr OutputInfo kOuts[] = {
    {"out_l"},
    {"out_r"}
};

class Eq final : public Module {
 public:
  void init(double rate, const Config& cfg) override {
    (void)cfg;
    rate_ = static_cast<float>(rate);
    
    // Initialize parameter smoothing (20 ms)
    hp_freq_smooth_.set_time(0.020f, rate_);
    low_freq_smooth_.set_time(0.020f, rate_);
    low_gain_smooth_.set_time(0.020f, rate_);
    mid1_freq_smooth_.set_time(0.020f, rate_);
    mid1_gain_smooth_.set_time(0.020f, rate_);
    mid1_q_smooth_.set_time(0.020f, rate_);
    mid2_freq_smooth_.set_time(0.020f, rate_);
    mid2_gain_smooth_.set_time(0.020f, rate_);
    mid2_q_smooth_.set_time(0.020f, rate_);
    high_freq_smooth_.set_time(0.020f, rate_);
    high_gain_smooth_.set_time(0.020f, rate_);
    
    // Set initial values
    hp_freq_smooth_.init(20.0f);
    low_freq_smooth_.init(120.0f);
    low_gain_smooth_.init(0.0f);
    mid1_freq_smooth_.init(500.0f);
    mid1_gain_smooth_.init(0.0f);
    mid1_q_smooth_.init(0.707f);
    mid2_freq_smooth_.init(2500.0f);
    mid2_gain_smooth_.init(0.0f);
    mid2_q_smooth_.init(0.707f);
    high_freq_smooth_.init(10000.0f);
    high_gain_smooth_.init(0.0f);
    
    // Clear filter states
    state_hp_l_.clear();   state_hp_r_.clear();
    state_low_l_.clear();  state_low_r_.clear();
    state_mid1_l_.clear(); state_mid1_r_.clear();
    state_mid2_l_.clear(); state_mid2_r_.clear();
    state_high_l_.clear(); state_high_r_.clear();
  }

  void process(const float* ins, float* outs) override {
    // Clamping limits
    const float max_freq = 0.45f * rate_;
    
    // Sanitize and clamp control parameters
    const float raw_hp_freq = sanitize_clamp(ins[2], 5.0f, max_freq, 20.0f);
    const float raw_low_freq = sanitize_clamp(ins[3], 5.0f, max_freq, 120.0f);
    const float raw_low_gain = sanitize_clamp(ins[4], -24.0f, 24.0f, 0.0f);
    const float raw_mid1_freq = sanitize_clamp(ins[5], 5.0f, max_freq, 500.0f);
    const float raw_mid1_gain = sanitize_clamp(ins[6], -24.0f, 24.0f, 0.0f);
    const float raw_mid1_q = sanitize_clamp(ins[7], 0.1f, 18.0f, 0.707f);
    const float raw_mid2_freq = sanitize_clamp(ins[8], 5.0f, max_freq, 2500.0f);
    const float raw_mid2_gain = sanitize_clamp(ins[9], -24.0f, 24.0f, 0.0f);
    const float raw_mid2_q = sanitize_clamp(ins[10], 0.1f, 18.0f, 0.707f);
    const float raw_high_freq = sanitize_clamp(ins[11], 5.0f, max_freq, 10000.0f);
    const float raw_high_gain = sanitize_clamp(ins[12], -24.0f, 24.0f, 0.0f);
    
    // Apply smoothing to parameters
    const float hp_freq = hp_freq_smooth_.process(raw_hp_freq);
    const float low_freq = low_freq_smooth_.process(raw_low_freq);
    const float low_gain = low_gain_smooth_.process(raw_low_gain);
    const float mid1_freq = mid1_freq_smooth_.process(raw_mid1_freq);
    const float mid1_gain = mid1_gain_smooth_.process(raw_mid1_gain);
    const float mid1_q = mid1_q_smooth_.process(raw_mid1_q);
    const float mid2_freq = mid2_freq_smooth_.process(raw_mid2_freq);
    const float mid2_gain = mid2_gain_smooth_.process(raw_mid2_gain);
    const float mid2_q = mid2_q_smooth_.process(raw_mid2_q);
    const float high_freq = high_freq_smooth_.process(raw_high_freq);
    const float high_gain = high_gain_smooth_.process(raw_high_gain);
    
    // Compute shared coefficients
    const BiquadCoeffs c_hp = make_hpf(hp_freq, rate_);
    const BiquadCoeffs c_low = make_low_shelf(low_freq, low_gain, rate_);
    const BiquadCoeffs c_mid1 = make_peaking_eq(mid1_freq, mid1_gain, mid1_q, rate_);
    const BiquadCoeffs c_mid2 = make_peaking_eq(mid2_freq, mid2_gain, mid2_q, rate_);
    const BiquadCoeffs c_high = make_high_shelf(high_freq, high_gain, rate_);
    
    // Left channel cascade processing
    float x_l = sanitize(ins[0], 0.0f);
    x_l = Biquad::process(x_l, c_hp, state_hp_l_);
    x_l = Biquad::process(x_l, c_low, state_low_l_);
    x_l = Biquad::process(x_l, c_mid1, state_mid1_l_);
    x_l = Biquad::process(x_l, c_mid2, state_mid2_l_);
    x_l = Biquad::process(x_l, c_high, state_high_l_);
    outs[0] = x_l;
    
    // Right channel cascade processing
    float x_r = sanitize(ins[1], 0.0f);
    x_r = Biquad::process(x_r, c_hp, state_hp_r_);
    x_r = Biquad::process(x_r, c_low, state_low_r_);
    x_r = Biquad::process(x_r, c_mid1, state_mid1_r_);
    x_r = Biquad::process(x_r, c_mid2, state_mid2_r_);
    x_r = Biquad::process(x_r, c_high, state_high_r_);
    outs[1] = x_r;
  }

  void copy_state_from(const Module& other) override {
    const auto& o = static_cast<const Eq&>(other);
    hp_freq_smooth_ = o.hp_freq_smooth_;
    low_freq_smooth_ = o.low_freq_smooth_;
    low_gain_smooth_ = o.low_gain_smooth_;
    mid1_freq_smooth_ = o.mid1_freq_smooth_;
    mid1_gain_smooth_ = o.mid1_gain_smooth_;
    mid1_q_smooth_ = o.mid1_q_smooth_;
    mid2_freq_smooth_ = o.mid2_freq_smooth_;
    mid2_gain_smooth_ = o.mid2_gain_smooth_;
    mid2_q_smooth_ = o.mid2_q_smooth_;
    high_freq_smooth_ = o.high_freq_smooth_;
    high_gain_smooth_ = o.high_gain_smooth_;
    
    state_hp_l_ = o.state_hp_l_;     state_hp_r_ = o.state_hp_r_;
    state_low_l_ = o.state_low_l_;   state_low_r_ = o.state_low_r_;
    state_mid1_l_ = o.state_mid1_l_; state_mid1_r_ = o.state_mid1_r_;
    state_mid2_l_ = o.state_mid2_l_; state_mid2_r_ = o.state_mid2_r_;
    state_high_l_ = o.state_high_l_; state_high_r_ = o.state_high_r_;
  }

 private:
  float rate_ = 48000.0f;
  
  // Parameter smoothing states
  OnePole hp_freq_smooth_;
  OnePole low_freq_smooth_;
  OnePole low_gain_smooth_;
  OnePole mid1_freq_smooth_;
  OnePole mid1_gain_smooth_;
  OnePole mid1_q_smooth_;
  OnePole mid2_freq_smooth_;
  OnePole mid2_gain_smooth_;
  OnePole mid2_q_smooth_;
  OnePole high_freq_smooth_;
  OnePole high_gain_smooth_;
  
  // Left biquad states
  BiquadState state_hp_l_;
  BiquadState state_low_l_;
  BiquadState state_mid1_l_;
  BiquadState state_mid2_l_;
  BiquadState state_high_l_;
  
  // Right biquad states
  BiquadState state_hp_r_;
  BiquadState state_low_r_;
  BiquadState state_mid1_r_;
  BiquadState state_mid2_r_;
  BiquadState state_high_r_;
};

std::unique_ptr<Module> make() { return std::make_unique<Eq>(); }

}  // namespace

const ModuleInfo eq_info{"eq", kIns, {}, kOuts, &make};

}  // namespace ms::dsp
