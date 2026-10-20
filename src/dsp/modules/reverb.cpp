#include <algorithm>
#include <cmath>
#include <cstddef>
#include <numbers>
#include <vector>
#include "dsp/input_key.h"
#include "dsp/modules.h"

// Paper-derived Dattorro 1997 plate: topology, delay lengths, coefficients,
// and output taps, scaled from the paper's 29761 Hz reference.
// See docs/attribution.md.

namespace ms::dsp {
namespace {

constexpr InputInfo kIns[] = {{"in_l", 0.f},  {"in_r", 0.f},
                              {"decay", 0.5f}, {"damp", 0.25f},
                              {"mix", 0.3f},   {"freeze", 0.f}};
constexpr ConfigInfo kCfg[] = {{"predelay", nullptr},
                               {"bandwidth", nullptr},
                               {"mod_rate", nullptr},
                               {"mod_depth", nullptr}};
constexpr OutputInfo kOuts[] = {{"out_l"}, {"out_r"}};

enum LineId {
  kPre = 0, kIn1, kIn2, kIn3, kIn4,
  kApA1, kDelA1, kApA2, kDelA2,
  kApB1, kDelB1, kApB2, kDelB2,
  kLineCount
};
constexpr double kBaseLen[kLineCount] = {7440, 142, 107, 379, 277,
                                         672, 4453, 1800, 3720,
                                         908, 4217, 2656, 3163};
constexpr double kRefRate = 29761.0;

struct TapSpec { int line; double base; float sign; };
constexpr TapSpec kTapsL[7] = {
    {kDelB1, 266, 1.f},  {kDelB1, 2974, 1.f}, {kApB2, 1913, -1.f},
    {kDelB2, 1996, 1.f}, {kDelA1, 1990, -1.f}, {kApA2, 187, -1.f},
    {kDelA2, 1066, -1.f}};
constexpr TapSpec kTapsR[7] = {
    {kDelA1, 353, 1.f},  {kDelA1, 3627, 1.f}, {kApA2, 1228, -1.f},
    {kDelA2, 2673, 1.f}, {kDelB1, 2111, -1.f}, {kApB2, 335, -1.f},
    {kDelB2, 121, -1.f}};

class Reverb final : public Module {
 public:
  void init(double rate, const Config& cfg) override {
    rate_ = static_cast<float>(rate);
    const double k = rate / kRefRate;
    std::size_t off = 0;
    for (int i = 0; i < kLineCount; ++i) {
      auto len = static_cast<std::size_t>(std::max(2.0, std::round(kBaseLen[i] * k)));
      if (i == kApA1 || i == kApB1) len += kMaxExcursion + 8;
      ln_[i] = {off, len, 0};
      off += len;
    }
    arena_.assign(off, 0.0f);  // control thread: allocation is legal here
    lenA1_ = static_cast<std::size_t>(std::round(kBaseLen[kApA1] * k));
    lenB1_ = static_cast<std::size_t>(std::round(kBaseLen[kApB1] * k));
    for (int t = 0; t < 7; ++t) {
      tapL_[t] = scaled_tap(kTapsL[t], k);
      tapR_[t] = scaled_tap(kTapsR[t], k);
    }
    double pre = cfg_num(cfg, "predelay", 0.02);
    if (!std::isfinite(pre)) pre = 0.02;
    pre = std::clamp(pre, 0.0, 0.25);
    pre_back_ = std::clamp<std::size_t>(
        static_cast<std::size_t>(pre * rate), 1, ln_[kPre].size - 1);
    bw_ = static_cast<float>(std::clamp(cfg_num(cfg, "bandwidth", 0.9995), 0.0, 1.0));
    mod_rate_ = static_cast<float>(std::clamp(cfg_num(cfg, "mod_rate", 1.0), 0.0, 20.0));
    mod_depth_ = static_cast<float>(std::clamp(cfg_num(cfg, "mod_depth", 13.0), 0.0,
                                               static_cast<double>(kMaxExcursion)));
    // The modulated taps read at len - depth; keep depth below both lengths
    // so the invariant holds even at absurdly low sample rates.
    const float max_safe = static_cast<float>(std::min(lenA1_, lenB1_)) - 2.0f;
    mod_depth_ = std::max(0.0f, std::min(mod_depth_, max_safe));
    lp_in_ = lpA_ = lpB_ = 0.0f;
    lfo_ = 0.0;
    freeze_ = 0.0f;
  }

  void process(const float* ins, float* outs) override {
    // Smoothed freeze blend (τ = 10 ms). While frozen: loop gain 1, damping
    // off, tank input muted — the paper's sanctioned indefinite hold.
    const float ftarget = ins[5] > 0.5f ? 1.0f : 0.0f;
    const float fa = 1.0f - std::exp(-1.0f / (0.01f * rate_));
    freeze_ += (ftarget - freeze_) * fa;
    if (freeze_ < 1e-20f) freeze_ = 0.0f;

    const float decay_in = std::clamp(sanitize(ins[2]), 0.0f, 0.9995f);
    const float decay = decay_in + (1.0f - decay_in) * freeze_;
    const float damp = std::clamp(sanitize(ins[3]), 0.0f, 1.0f) * (1.0f - freeze_);
    const float kd2 = std::clamp(decay + 0.15f, 0.25f, 0.5f);

    // mono sum -> predelay -> bandwidth lowpass -> 4 input diffusers
    // Non-finite input never enters the tank: it would circulate forever.
    float x = 0.5f * (sanitize(ins[0]) + sanitize(ins[1])) * (1.0f - freeze_);
    const float delayed = dread(ln_[kPre], pre_back_);
    dwrite(ln_[kPre], x);
    x = delayed;
    lp_in_ = bw_ * x + (1.0f - bw_) * lp_in_;
    if (std::abs(lp_in_) < 1e-20f) lp_in_ = 0.0f;
    x = lp_in_;
    x = allpass(ln_[kIn1], 0.75f, x);
    x = allpass(ln_[kIn2], 0.75f, x);
    x = allpass(ln_[kIn3], 0.625f, x);
    x = allpass(ln_[kIn4], 0.625f, x);

    // Quadrature LFOs smear the tank allpass taps (kills the metallic
    // mode fence; linear interp per the paper — its tiny time-varying
    // lowpass is inaudible at these excursions).
    const auto ph = static_cast<float>(lfo_);
    const float twopi = 2.0f * std::numbers::pi_v<float>;
    const float excA = mod_depth_ * std::sin(twopi * ph);
    const float excB = mod_depth_ * std::cos(twopi * ph);
    lfo_ += static_cast<double>(mod_rate_) / static_cast<double>(rate_);
    lfo_ -= std::floor(lfo_);

    // Cross-feedback: each branch is fed the other's final delay output.
    const float fbA = decay * dread(ln_[kDelB2], ln_[kDelB2].size);
    const float fbB = decay * dread(ln_[kDelA2], ln_[kDelA2].size);

    // Branch A: mod allpass -> delay -> damping -> ×decay -> allpass -> delay
    float a = allpass_mod(ln_[kApA1], static_cast<float>(lenA1_) + excA, 0.70f,
                          x + fbA);
    const float dA = dread(ln_[kDelA1], ln_[kDelA1].size);
    dwrite(ln_[kDelA1], a);
    lpA_ = (1.0f - damp) * dA + damp * lpA_;
    if (std::abs(lpA_) < 1e-20f) lpA_ = 0.0f;
    a = allpass(ln_[kApA2], kd2, lpA_ * decay);
    dwrite(ln_[kDelA2], a);

    // Branch B mirrors with the quadrature LFO.
    float b = allpass_mod(ln_[kApB1], static_cast<float>(lenB1_) + excB, 0.70f,
                          x + fbB);
    const float dB = dread(ln_[kDelB1], ln_[kDelB1].size);
    dwrite(ln_[kDelB1], b);
    lpB_ = (1.0f - damp) * dB + damp * lpB_;
    if (std::abs(lpB_) < 1e-20f) lpB_ = 0.0f;
    b = allpass(ln_[kApB2], kd2, lpB_ * decay);
    dwrite(ln_[kDelB2], b);

    // 7 tank taps per side (paper Table 2) -> wide image from a mono tank.
    float wl = 0.0f, wr = 0.0f;
    for (int t = 0; t < 7; ++t) {
      wl += kTapsL[t].sign * dread(ln_[kTapsL[t].line], tapL_[t]);
      wr += kTapsR[t].sign * dread(ln_[kTapsR[t].line], tapR_[t]);
    }
    wl *= 0.6f;
    wr *= 0.6f;

    if (mix_key_.changed({ins[4]})) {  // skip cos/sin while mix holds
      const float m = std::clamp(ins[4], 0.0f, 1.0f);
      const float ang = m * (std::numbers::pi_v<float> / 2.0f);
      dry_ = std::cos(ang);
      wet_ = std::sin(ang);
    }
    const float dry = dry_, wet = wet_;
    outs[0] = ins[0] * dry + wl * wet;
    outs[1] = ins[1] * dry + wr * wet;
  }

  void copy_state_from(const Module& other) override {
    const auto& o = static_cast<const Reverb&>(other);
    // RT-called: one bounded copy. Same-rate migration means equal sizes;
    // min() is defense if they ever differ (then positions stay our own).
    const std::size_t n = std::min(arena_.size(), o.arena_.size());
    std::copy_n(o.arena_.begin(), n, arena_.begin());
    if (arena_.size() == o.arena_.size()) {
      for (int i = 0; i < kLineCount; ++i) ln_[i].pos = o.ln_[i].pos;
    }
    lp_in_ = o.lp_in_;
    lpA_ = o.lpA_;
    lpB_ = o.lpB_;
    lfo_ = o.lfo_;
    freeze_ = o.freeze_;
  }

 private:
  static constexpr std::size_t kMaxExcursion = 100;
  struct Line { std::size_t off; std::size_t size; std::size_t pos; };

  std::size_t scaled_tap(const TapSpec& t, double k) const {
    const auto len = ln_[t.line].size;
    const auto raw = static_cast<std::size_t>(std::round(t.base * k));
    return std::clamp<std::size_t>(raw, 1, len - 1);
  }
  float dread(const Line& l, std::size_t back) const {
    std::size_t i = l.pos + l.size - back;
    if (i >= l.size) i -= l.size;
    return arena_[l.off + i];
  }
  float dread_frac(const Line& l, float back) const {
    const auto b0 = static_cast<std::size_t>(back);
    const float f = back - static_cast<float>(b0);
    const float y0 = dread(l, b0);
    const float y1 = dread(l, b0 + 1);
    return y0 + (y1 - y0) * f;
  }
  void dwrite(Line& l, float x) {
    arena_[l.off + l.pos] = x;
    l.pos = l.pos + 1 == l.size ? 0 : l.pos + 1;
  }
  // Schroeder allpass over a full line: read the oldest sample, write the
  // combined input, return the allpassed output.
  float allpass(Line& l, float g, float x) {
    const float z = dread(l, l.size);
    const float v = x + g * z;
    dwrite(l, v);
    return z - g * v;
  }
  float allpass_mod(Line& l, float back, float g, float x) {
    const float z = dread_frac(l, back);
    const float v = x + g * z;
    dwrite(l, v);
    return z - g * v;
  }

  float rate_ = 48000.0f;
  InputKey<1> mix_key_;
  float dry_ = 1.0f, wet_ = 0.0f;
  std::vector<float> arena_;
  Line ln_[kLineCount] = {};
  std::size_t lenA1_ = 0, lenB1_ = 0, pre_back_ = 1;
  std::size_t tapL_[7] = {}, tapR_[7] = {};
  float bw_ = 0.9995f, mod_rate_ = 1.0f, mod_depth_ = 13.0f;
  float lp_in_ = 0.0f, lpA_ = 0.0f, lpB_ = 0.0f;
  double lfo_ = 0.0;
  float freeze_ = 0.0f;
};

std::unique_ptr<Module> make() { return std::make_unique<Reverb>(); }
}  // namespace

const ModuleInfo reverb_info{"reverb", kIns, kCfg, kOuts, &make};
}  // namespace ms::dsp
