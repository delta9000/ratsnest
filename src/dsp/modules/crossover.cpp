#include <algorithm>
#include "dsp/biquad.h"
#include "dsp/input_key.h"
#include "dsp/mastering.h"
#include "dsp/modules.h"

// Three-band Linkwitz-Riley (LR4) crossover: each split is two cascaded
// Butterworth biquads; the low band passes the upper split's allpass so the
// bands sum flat. Paper-derived (Linkwitz 1976); see docs/attribution.md.

namespace ms::dsp {
namespace {

constexpr InputInfo kIns[] = {{"in_l", 0.0f}, {"in_r", 0.0f}, {"low", 200.0f}, {"high", 2000.0f}};
constexpr OutputInfo kOuts[] = {{"low_l"}, {"low_r"}, {"mid_l"},
                                {"mid_r"}, {"high_l"}, {"high_r"}};

class Crossover final : public Module {
 public:
  void init(double rate, const Config&) override {
    rate_ = static_cast<float>(rate);
    for (Chan& c : ch_) c = {};
  }

  void process(const float* ins, float* outs) override {
    if (freq_key_.changed({ins[2], ins[3]})) {
      const float lo = sanitize_clamp(ins[2], 20.0f, 0.45f * rate_, 200.0f);
      const float hi = std::max(lo, sanitize_clamp(ins[3], 20.0f, 0.45f * rate_, 2000.0f));
      lp1_ = make_lpf(lo, rate_);
      hp1_ = make_hpf(lo, rate_);
      lp2_ = make_lpf(hi, rate_);
      hp2_ = make_hpf(hi, rate_);
      ap2_ = make_apf(hi, rate_);
    }
    for (int c = 0; c < 2; ++c) {
      Chan& s = ch_[c];
      const float x = sanitize(ins[c], 0.0f);
      const float low = run(run(x, lp1_, s.st[0]), lp1_, s.st[1]);
      const float rest = run(run(x, hp1_, s.st[2]), hp1_, s.st[3]);
      outs[0 + c] = Biquad::process(low, ap2_, s.st[4]);
      outs[2 + c] = run(run(rest, lp2_, s.st[5]), lp2_, s.st[6]);
      outs[4 + c] = run(run(rest, hp2_, s.st[7]), hp2_, s.st[8]);
    }
  }

  void copy_state_from(const Module& other) override {
    const auto& o = static_cast<const Crossover&>(other);
    ch_[0] = o.ch_[0];
    ch_[1] = o.ch_[1];
  }

 private:
  struct Chan { BiquadState st[9]; };
  static float run(float x, const BiquadCoeffs& c, BiquadState& s) {
    return Biquad::process(x, c, s);
  }
  float rate_ = 48000.0f;
  Chan ch_[2];
  InputKey<2> freq_key_;
  BiquadCoeffs lp1_, hp1_, lp2_, hp2_, ap2_;
};

std::unique_ptr<Module> make() { return std::make_unique<Crossover>(); }
}  // namespace

const ModuleInfo crossover_info{"crossover", kIns, {}, kOuts, &make};
}  // namespace ms::dsp
