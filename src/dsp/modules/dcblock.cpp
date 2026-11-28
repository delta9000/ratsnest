#include <algorithm>
#include <cmath>
#include <numbers>
#include "dsp/input_key.h"
#include "dsp/modules.h"
#include "dsp/mastering.h"

namespace ms::dsp {
namespace {

constexpr InputInfo kIns[] = {
    {"in_l", 0.f},
    {"in_r", 0.f},
    in_hz("cutoff", 10.f)
};
constexpr OutputInfo kOuts[] = {
    {"out_l"},
    {"out_r"}
};

class DcBlock final : public Module {
 public:
  void init(double rate, const Config& cfg) override {
    (void)cfg;
    rate_ = static_cast<float>(rate);
    
    // Cutoff smoothing; the first process() snaps it to its target.
    cutoff_smooth_.set_time(0.020f, rate_);
    primed_ = false;
    
    // Initialize state
    lp_l_ = 0.0f;
    lp_r_ = 0.0f;
  }

  void process(const float* ins, float* outs) override {
    // Sanitize inputs to protect against NaN, Inf, and invalid cutoff values
    const float raw_in_l = sanitize(ins[0], 0.0f);
    const float raw_in_r = sanitize(ins[1], 0.0f);
    const float raw_cutoff = sanitize_clamp(ins[2], 1.0f, 40.0f, 10.0f);
    
    // Smooth the cutoff parameter, starting settled at the first target
    if (!primed_) cutoff_smooth_.init(raw_cutoff);
    primed_ = true;
    const float cutoff = cutoff_smooth_.process(raw_cutoff);
    
    // Compute alpha coefficient based on sample rate and cutoff
    // alpha = exp(-2 * pi * cutoff / rate_)
    if (cutoff_key_.changed({cutoff})) {  // skip exp once the smoother settles
      const float alpha = std::exp(-2.0f * std::numbers::pi_v<float> * cutoff / rate_);
      lp_coeff_ = 1.0f - alpha;
    }
    const float lp_coeff = lp_coeff_;
    
    // Update left channel low-pass state
    lp_l_ += lp_coeff * (raw_in_l - lp_l_);
    lp_l_ = flush_denormal(lp_l_);
    outs[0] = raw_in_l - lp_l_;
    
    // Update right channel low-pass state
    lp_r_ += lp_coeff * (raw_in_r - lp_r_);
    lp_r_ = flush_denormal(lp_r_);
    outs[1] = raw_in_r - lp_r_;
  }

  void copy_state_from(const Module& other) override {
    const auto& o = static_cast<const DcBlock&>(other);
    lp_l_ = o.lp_l_;
    lp_r_ = o.lp_r_;
    cutoff_smooth_ = o.cutoff_smooth_;
    primed_ = o.primed_;
  }

 private:
  float rate_ = 48000.0f;
  OnePole cutoff_smooth_;
  bool primed_ = false;
  float lp_l_ = 0.0f;
  float lp_r_ = 0.0f;
  InputKey<1> cutoff_key_;
  float lp_coeff_ = 0.0f;
};

std::unique_ptr<Module> make() { return std::make_unique<DcBlock>(); }

}  // namespace

const ModuleInfo dcblock_info{"dcblock", kIns, {}, kOuts, &make};

}  // namespace ms::dsp
