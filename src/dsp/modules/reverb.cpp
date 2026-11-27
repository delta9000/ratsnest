#include <algorithm>
#include <cmath>
#include <cstddef>
#include <numbers>
#include <vector>
#include "dsp/input_key.h"
#include "dsp/modules.h"

// Paper-derived Dattorro 1997 plate: topology, delay lengths, coefficients,
// and output taps, scaled from the paper's 29761 Hz reference. `model: hall`
// is an eight-line feedback delay network after Jot and Chaigne (1991) with a
// Householder mix, fed by stereo early reflections. See docs/attribution.md.

namespace ms::dsp {
namespace {

constexpr InputInfo kIns[] = {{"in_l", 0.f},  {"in_r", 0.f},
                              {"decay", 0.5f}, {"damp", 0.25f},
                              {"mix", 0.3f},   {"freeze", 0.f}};
constexpr const char* kModels[] = {"plate", "hall", nullptr};
constexpr ConfigInfo kCfg[] = {{"model", kModels},
                               {"size", nullptr},
                               {"predelay", nullptr},
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

// Hall: delay lengths in ms at size 1 (mutually prime in samples at the
// common rates, spread over ~1.3 octaves), early-reflection taps (ms; even
// taps read the same side, odd ones the other), and the two input diffusers.
constexpr int kFdn = 8;
constexpr double kFdnMs[kFdn] = {43.1, 47.9, 53.7, 59.3, 67.1, 73.9, 83.3, 97.1};
constexpr int kEr = 8;
constexpr double kErMs[2][kEr] = {{7.1, 11.3, 17.9, 23.5, 31.7, 41.3, 53.9, 67.1},
                                  {8.3, 12.7, 19.1, 26.9, 34.1, 44.9, 57.1, 71.3}};
constexpr float kErGain[kEr] = {0.84f, -0.71f, 0.62f, -0.53f, 0.45f, -0.38f, 0.32f, -0.27f};
constexpr double kDiffMs[2] = {4.7, 3.1};
// Seconds per factor of `decay` at size 1, so a hall and a plate at the same
// `decay` ring for about the same time (measured: the plate's RT60).
constexpr double kHallRef = 0.30;

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
    hall_ = cfg_enum(cfg, "model", kModels) == 1;
    double size = cfg_num(cfg, "size", 1.0);
    size = std::isfinite(size) ? std::clamp(size, 0.25, 4.0) : 1.0;
    const double k = rate / kRefRate * size;
    std::size_t off = 0;
    if (hall_) off = init_hall(rate, size);
    for (int i = 0; i < kLineCount && !hall_; ++i) {
      auto len = static_cast<std::size_t>(std::max(2.0, std::round(kBaseLen[i] * k)));
      if (i == kApA1 || i == kApB1) len += kMaxExcursion + 8;
      ln_[i] = {off, len, 0};
      off += len;
    }
    // The predelay line is the plate's kPre; the hall keeps one per side.
    const std::size_t pre_len = static_cast<std::size_t>(0.25 * rate) + 2;
    for (int c = 0; c < 2; ++c) {
      pre_[c] = {off, pre_len, 0};
      off += pre_len;
    }
    if (!hall_) ln_[kPre] = pre_[0];
    arena_.assign(off, 0.0f);  // control thread: allocation is legal here
    lenA1_ = static_cast<std::size_t>(std::round(kBaseLen[kApA1] * k));
    lenB1_ = static_cast<std::size_t>(std::round(kBaseLen[kApB1] * k));
    for (int t = 0; t < 7 && !hall_; ++t) {
      tapL_[t] = scaled_tap(kTapsL[t], k);
      tapR_[t] = scaled_tap(kTapsR[t], k);
    }
    double pre = cfg_num(cfg, "predelay", 0.02);
    if (!std::isfinite(pre)) pre = 0.02;
    pre = std::clamp(pre, 0.0, 0.25);
    pre_back_ = std::clamp<std::size_t>(
        static_cast<std::size_t>(pre * rate), 1, pre_len - 1);
    bw_ = static_cast<float>(std::clamp(cfg_num(cfg, "bandwidth", 0.9995), 0.0, 1.0));
    mod_rate_ = static_cast<float>(std::clamp(cfg_num(cfg, "mod_rate", 1.0), 0.0, 20.0));
    mod_depth_ = static_cast<float>(std::clamp(cfg_num(cfg, "mod_depth", 13.0), 0.0,
                                               static_cast<double>(kMaxExcursion)));
    // The modulated taps read at len - depth; keep depth below both lengths
    // so the invariant holds even at absurdly low sample rates.
    const float max_safe =
        static_cast<float>(hall_ ? fdn_len_[0] : std::min(lenA1_, lenB1_)) - 2.0f;
    mod_depth_ = std::max(0.0f, std::min(mod_depth_, max_safe));
    lp_in_ = lpA_ = lpB_ = lp_r_ = 0.0f;
    std::fill(std::begin(fdn_lp_), std::end(fdn_lp_), 0.0f);
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

    // Quadrature LFOs smear the tank's modulated taps (kills the metallic
    // mode fence; linear interp per the paper — its tiny time-varying
    // lowpass is inaudible at these excursions).
    const auto ph = static_cast<float>(lfo_);
    const float twopi = 2.0f * std::numbers::pi_v<float>;
    const float excA = mod_depth_ * std::sin(twopi * ph);
    const float excB = mod_depth_ * std::cos(twopi * ph);
    lfo_ += static_cast<double>(mod_rate_) / static_cast<double>(rate_);
    lfo_ -= std::floor(lfo_);

    // Non-finite input never enters the tank: it would circulate forever.
    const float mute = 1.0f - freeze_;
    float wl = 0.0f, wr = 0.0f;
    if (hall_) {
      hall(sanitize(ins[0]) * mute, sanitize(ins[1]) * mute, decay, damp, excA, excB, wl, wr);
    } else {
      plate(0.5f * (sanitize(ins[0]) + sanitize(ins[1])) * mute, decay, damp, excA, excB, wl,
            wr);
    }

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

 private:
  // mono sum -> predelay -> bandwidth lowpass -> 4 input diffusers -> tank
  void plate(float x, float decay, float damp, float excA, float excB, float& wl, float& wr) {
    const float kd2 = std::clamp(decay + 0.15f, 0.25f, 0.5f);
    x = bandlimit(predelay(0, x), lp_in_);
    x = allpass(ln_[kIn1], 0.75f, x);
    x = allpass(ln_[kIn2], 0.75f, x);
    x = allpass(ln_[kIn3], 0.625f, x);
    x = allpass(ln_[kIn4], 0.625f, x);

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
    for (int t = 0; t < 7; ++t) {
      wl += kTapsL[t].sign * dread(ln_[kTapsL[t].line], tapL_[t]);
      wr += kTapsR[t].sign * dread(ln_[kTapsR[t].line], tapR_[t]);
    }
    wl *= 0.6f;
    wr *= 0.6f;
  }

  // Stereo predelay -> bandwidth lowpass -> early reflections (true stereo:
  // odd taps cross sides) -> two diffusers per side -> eight-line FDN. Left
  // feeds the even lines and right the odd ones; the Householder mix
  // (I - 2/N) couples them all, and each side taps its own lines out.
  void hall(float xl, float xr, float decay, float damp, float excA, float excB, float& wl,
            float& wr) {
    float x[2] = {bandlimit(predelay(0, xl), lp_in_), bandlimit(predelay(1, xr), lp_r_)};
    for (int c = 0; c < 2; ++c) dwrite(er_[c], x[c]);
    float in[2] = {0.0f, 0.0f};
    for (int c = 0; c < 2; ++c) {
      // Odd taps come off the far wall: from the other side, quieter.
      for (int t = 0; t < kEr; ++t)
        in[c] += kErGain[t] * (t & 1 ? 0.6f : 1.0f) * dread(er_[c ^ (t & 1)], er_tap_[c][t]);
      in[c] *= 0.5f;
    }
    if (decay_key_.changed({decay})) {
      for (int i = 0; i < kFdn; ++i)
        fdn_g_[i] = static_cast<float>(std::pow(static_cast<double>(decay), fdn_exp_[i]));
    }
    const float exc[4] = {excA, excB, -excA, -excB};
    float y[kFdn], sum = 0.0f;
    for (int i = 0; i < kFdn; ++i) {
      const float back = static_cast<float>(fdn_len_[i]) + (i < 4 ? exc[i] : 0.0f);
      y[i] = dread_frac(fdn_[i], back);
      fdn_lp_[i] = flush_denormal((1.0f - damp) * y[i] + damp * fdn_lp_[i]);
      sum += fdn_g_[i] * fdn_lp_[i];
    }
    const float wet[2] = {allpass(diff_[0][1], 0.6f, allpass(diff_[0][0], 0.6f, in[0])),
                          allpass(diff_[1][1], 0.6f, allpass(diff_[1][0], 0.6f, in[1]))};
    sum *= 2.0f / kFdn;
    for (int i = 0; i < kFdn; ++i) dwrite(fdn_[i], fdn_g_[i] * fdn_lp_[i] - sum + wet[i & 1]);
    for (int i = 0; i < kFdn; i += 2) {
      const float sign = (i & 2) ? -1.0f : 1.0f;
      wl += sign * y[i];
      wr += sign * y[i + 1];
    }
    wl = 0.45f * wl + 0.5f * in[0];
    wr = 0.45f * wr + 0.5f * in[1];
  }

  // Lays out the hall's lines from `off` = 0; returns the arena used.
  std::size_t init_hall(double rate, double size) {
    std::size_t off = 0;
    auto line = [&](double ms, std::size_t extra) {
      const auto len = static_cast<std::size_t>(std::max(2.0, std::round(ms * 0.001 * size * rate)));
      const Line l{off, len + extra, 0};
      off += len + extra;
      return std::make_pair(l, len);
    };
    for (int i = 0; i < kFdn; ++i) {
      std::tie(fdn_[i], fdn_len_[i]) = line(kFdnMs[i], kMaxExcursion + 8);
      fdn_exp_[i] = static_cast<double>(fdn_len_[i]) / rate / (kHallRef * size);
    }
    for (int c = 0; c < 2; ++c) {
      for (int t = 0; t < kEr; ++t)
        er_tap_[c][t] = static_cast<std::size_t>(std::max(1.0, kErMs[c][t] * 0.001 * size * rate));
      er_[c] = line(kErMs[1][kEr - 1] * 1.05, 2).first;
      for (int d = 0; d < 2; ++d) diff_[c][d] = line(kDiffMs[d] * (c ? 1.13 : 1.0), 0).first;
    }
    decay_key_ = {};
    return off;
  }

  float predelay(int c, float x) {
    const float delayed = dread(pre_[c], pre_back_);
    dwrite(pre_[c], x);
    return delayed;
  }
  float bandlimit(float x, float& state) const {
    state = flush_denormal(bw_ * x + (1.0f - bw_) * state);
    return state;
  }

 public:

  void copy_state_from(const Module& other) override {
    const auto& o = static_cast<const Reverb&>(other);
    // RT-called: one bounded copy. Same-rate migration means equal sizes;
    // min() is defense if they ever differ (then positions stay our own).
    const std::size_t n = std::min(arena_.size(), o.arena_.size());
    std::copy_n(o.arena_.begin(), n, arena_.begin());
    if (arena_.size() == o.arena_.size()) {
      for (int i = 0; i < kLineCount; ++i) ln_[i].pos = o.ln_[i].pos;
    }
    for (int c = 0; c < 2; ++c) pre_[c].pos = o.pre_[c].pos;
    for (int c = 0; c < 2; ++c) er_[c].pos = o.er_[c].pos;
    for (int c = 0; c < 2; ++c)
      for (int d = 0; d < 2; ++d) diff_[c][d].pos = o.diff_[c][d].pos;
    for (int i = 0; i < kFdn; ++i) {
      fdn_[i].pos = o.fdn_[i].pos;
      fdn_lp_[i] = o.fdn_lp_[i];
    }
    lp_r_ = o.lp_r_;
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
  bool hall_ = false;
  Line pre_[2] = {}, er_[2] = {}, diff_[2][2] = {}, fdn_[kFdn] = {};
  std::size_t fdn_len_[kFdn] = {}, er_tap_[2][kEr] = {};
  double fdn_exp_[kFdn] = {};
  float fdn_g_[kFdn] = {}, fdn_lp_[kFdn] = {}, lp_r_ = 0.0f;
  InputKey<1> mix_key_, decay_key_;
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
