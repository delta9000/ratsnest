#include <algorithm>
#include <cmath>
#include "dsp/input_key.h"
#include "dsp/modules.h"
#include "dsp/biquad.h"
#include "dsp/mastering.h"

namespace ms::dsp {
namespace {

constexpr InputInfo kIns[] = {
    {"in_l", 0.0f},
    {"in_r", 0.0f},
    {"hp_freq", 0.0f},
    {"low_freq", 120.0f},
    {"low_gain", 0.0f},       // dB
    {"mid1_freq", 500.0f},
    {"mid1_gain", 0.0f},      // dB
    {"mid1_q", 0.707f},
    {"mid2_freq", 2500.0f},
    {"mid2_gain", 0.0f},      // dB
    {"mid2_q", 0.707f},
    {"high_freq", 10000.0f},
    {"high_gain", 0.0f},      // dB
    {"lp_freq", 0.0f},        // 0 = off
    {"low_q", 1.0f},
    {"high_q", 1.0f}
};
constexpr OutputInfo kOuts[] = {
    {"out_l"},
    {"out_r"}
};

// Parameter clamps, in kIns order from hp_freq; a negative ceiling means
// 0.45 * rate.
constexpr int kParams = 14;
constexpr float kLo[kParams] = {0, 5, -24, 5, -24, 0.1f, 5, -24, 0.1f, 5, -24, 0, 0.3f, 0.3f};
constexpr float kHi[kParams] = {-1, -1, 24, -1, 24, 18, -1, 24, 18, -1, 24, -1, 3, 3};

class Eq final : public Module {
 public:
  void init(double rate, const Config& cfg) override {
    (void)cfg;
    rate_ = static_cast<float>(rate);
    for (auto& s : smooth_) s.set_time(0.020f, rate_);  // 20 ms smoothing
    primed_ = false;
    for (auto& ch : st_)
      for (auto& s : ch) s.clear();
  }

  void process(const float* ins, float* outs) override {
    const float max_freq = 0.45f * rate_;
    float p[kParams];
    for (int i = 0; i < kParams; ++i)
      p[i] = sanitize_clamp(ins[i + 2], kLo[i], kHi[i] < 0 ? max_freq : kHi[i], kIns[i + 2].def);
    const bool hp_on = p[0] > 0.0f, lp_on = p[11] > 0.0f;

    // Snap to the first targets so a static patch starts settled, not
    // sweeping from the defaults; smooth only later motion.
    if (!primed_)
      for (int i = 0; i < kParams; ++i) smooth_[i].init(p[i]);
    primed_ = true;
    for (int i = 0; i < kParams; ++i) p[i] = smooth_[i].process(p[i]);

    update_coeffs(p);

    for (int ch = 0; ch < 2; ++ch) {
      float x = sanitize(ins[ch], 0.0f);
      if (hp_on) x = Biquad::process(x, c_[0], st_[ch][0]);
      else st_[ch][0].clear();
      for (int b = 1; b < 5; ++b) x = Biquad::process(x, c_[b], st_[ch][b]);
      if (lp_on) x = Biquad::process(x, c_[5], st_[ch][5]);
      else st_[ch][5].clear();
      outs[ch] = x;
    }
  }

  void copy_state_from(const Module& other) override {
    const auto& o = static_cast<const Eq&>(other);
    std::copy(std::begin(o.smooth_), std::end(o.smooth_), std::begin(smooth_));
    primed_ = o.primed_;
    for (int ch = 0; ch < 2; ++ch)
      std::copy(std::begin(o.st_[ch]), std::end(o.st_[ch]), std::begin(st_[ch]));
  }

 private:
  // Recompute a section's coefficients only when its smoothed inputs move.
  void update_coeffs(const float* p) {
    if (hp_key_.changed({p[0]})) c_[0] = make_hpf(std::max(p[0], 5.0f), rate_);
    if (low_key_.changed({p[1], p[2], p[12]})) c_[1] = make_low_shelf(p[1], p[2], rate_, p[12]);
    if (mid1_key_.changed({p[3], p[4], p[5]})) c_[2] = make_peaking_eq(p[3], p[4], p[5], rate_);
    if (mid2_key_.changed({p[6], p[7], p[8]})) c_[3] = make_peaking_eq(p[6], p[7], p[8], rate_);
    if (high_key_.changed({p[9], p[10], p[13]}))
      c_[4] = make_high_shelf(p[9], p[10], rate_, p[13]);
    if (lp_key_.changed({p[11]})) c_[5] = make_lpf(std::max(p[11], 5.0f), rate_);
  }

  float rate_ = 48000.0f;
  OnePole smooth_[kParams];
  bool primed_ = false;
  // Section order: hp, low shelf, mid1, mid2, high shelf, lp. Coefficients are
  // memoized on their smoothed inputs; see dsp/input_key.h.
  BiquadCoeffs c_[6];
  InputKey<1> hp_key_, lp_key_;
  InputKey<3> low_key_, high_key_, mid1_key_, mid2_key_;
  BiquadState st_[2][6];
};

std::unique_ptr<Module> make() { return std::make_unique<Eq>(); }

}  // namespace

const ModuleInfo eq_info{"eq", kIns, {}, kOuts, &make};

}  // namespace ms::dsp
