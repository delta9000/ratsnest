#pragma once
// Shared physical-resonator DSP core for the modal/string modules.
// Code-derived from Mutable Instruments Rings and stmlib (MIT), substantially
// adapted for ratsnest. See docs/attribution.md and THIRD_PARTY_NOTICES.md.
// Upstream: https://github.com/pichenettes/eurorack/tree/master/rings/dsp
#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <numbers>
#include <vector>
#include "dsp/util.h"

namespace ms::dsp::resonate {

inline float semitones_to_ratio(float x) {
  return std::exp2(x * (1.0f / 12.0f));
}

// Rings' lut_stiffness, evaluated directly. structure in 0..1.
inline float stiffness_curve(float g) {
  g = std::clamp(g, 0.0f, 0.9995f);
  if (g < 0.25f) return (g - 0.25f) * 0.25f;
  if (g < 0.3f) return 0.0f;
  if (g < 0.9f)
    return 0.01f * (std::pow(10.0f, (g - 0.3f) / 0.6f * 2.005f) - 1.0f);
  const float t = (g - 0.9f) * 10.0f;
  return 1.5f - 0.5f * std::cos(t * t * std::numbers::pi_v<float>);
}

// Rings' string-model structure fold: bipolar dispersion, dead zone at
// 0.24..0.26 (pure Karplus-Strong).
inline float dispersion_from_structure(float s) {
  s = std::clamp(s, 0.0f, 0.9995f);
  if (s < 0.24f) return (s - 0.24f) * 4.166f;
  if (s > 0.26f) return (s - 0.26f) * 1.35135f;
  return 0.0f;
}

// Simper trapezoidal state-variable filter (stmlib::Svf).
class Svf {
 public:
  void reset() { s1_ = s2_ = bp_ = lp_ = 0.0f; }
  // 5th-order odd polynomial tan() fit (Rings FREQUENCY_FAST): within ~1% to
  // ~13 kHz at 48 kHz, looser toward Nyquist. Monotone and finite over
  // 0..0.499, so hostile cutoffs stay stable even where the fit is loose.
  static float tan_fast(float f) {
    constexpr float pi = std::numbers::pi_v<float>;
    constexpr float a = 3.260e-01f * pi * pi * pi;
    constexpr float b = 1.823e-01f * pi * pi * pi * pi * pi;
    const float f2 = f * f;
    return f * (pi + f2 * (a + b * f2));
  }
  void set_f_q_fast(float f, float q) { set_g_q(tan_fast(f), q); }
  void set_f_q_accurate(float f, float q) {
    set_g_q(std::tan(std::numbers::pi_v<float> * f), q);
  }
  float process_bp(float in) {
    tick(in);
    return bp_;
  }
  float process_lp(float in) {
    tick(in);
    return lp_;
  }
  void flush_denormals() {
    if (std::abs(s1_) < 1e-20f) s1_ = 0.0f;
    if (std::abs(s2_) < 1e-20f) s2_ = 0.0f;
  }

 private:
  void set_g_q(float g, float q) {
    g_ = g;
    r_ = 1.0f / q;
    h_ = 1.0f / (1.0f + r_ * g_ + g_ * g_);
  }
  void tick(float in) {
    const float hp = (in - r_ * s1_ - g_ * s1_ - s2_) * h_;
    bp_ = g_ * hp + s1_;
    s1_ = g_ * hp + bp_;
    lp_ = g_ * bp_ + s2_;
    s2_ = g_ * bp_ + lp_;
  }
  float g_ = 0.0f, r_ = 1.0f, h_ = 0.5f;
  float s1_ = 0.0f, s2_ = 0.0f, bp_ = 0.0f, lp_ = 0.0f;
};

// Generates w_n = 0.5 + 0.5*cos(2*pi*n*x) for n = 0, 1, 2, ... via a
// two-multiply resonator recurrence; first next() returns exactly 1.
// The coefficient uses Rings' parabolic approximation of 2*cos(2*pi*x)
// (exact at x = 0, 0.25, 0.5).
class CosineSeries {
 public:
  explicit CosineSeries(float x) {
    float sign = 16.0f;
    x -= 0.25f;
    if (x < 0.0f) {
      x = -x;
    } else if (x > 0.5f) {
      x -= 0.5f;
    } else {
      sign = -16.0f;
    }
    c_ = sign * x * (1.0f - 2.0f * x);
    y0_ = 0.5f;
    y1_ = c_ * 0.25f;
  }
  float next() {
    const float t = y0_;
    y0_ = c_ * y0_ - y1_;
    y1_ = t;
    return t + 0.5f;
  }

 private:
  float c_, y0_, y1_;
};

// Power-of-two ring buffer. write() then read(k) returns the sample
// written k writes ago (read(0) = the sample just written), matching
// stmlib::DelayLine so the Rings loop structure ports one-to-one.
class DelayLine {
 public:
  void init(std::size_t min_size) {
    std::size_t size = 1;
    while (size < min_size) size <<= 1;
    buf_.assign(size, 0.0f);
    mask_ = size - 1;
    write_ = 0;
  }
  void clear() { std::fill(buf_.begin(), buf_.end(), 0.0f); }
  void write(float s) {
    write_ = (write_ - 1) & mask_;
    buf_[write_] = s;
  }
  float read(std::size_t delay) const { return buf_[(write_ + delay) & mask_]; }
  float read_linear(float delay) const {
    const auto di = static_cast<std::size_t>(delay);
    const float f = delay - static_cast<float>(di);
    return buf_[(write_ + di) & mask_] * (1.0f - f) +
           buf_[(write_ + di + 1) & mask_] * f;
  }
  float read_hermite(float delay) const {
    const auto di = static_cast<std::size_t>(delay);
    const float f = delay - static_cast<float>(di);
    const std::size_t t = write_ + di;
    const float xm1 = buf_[(t - 1) & mask_];
    const float x0 = buf_[t & mask_];
    const float x1 = buf_[(t + 1) & mask_];
    const float x2 = buf_[(t + 2) & mask_];
    const float c = (x1 - xm1) * 0.5f;
    const float v = x0 - x1;
    const float w = c + v;
    const float a = w + v + (x2 - x0) * 0.5f;
    const float b = w + a;
    return (((a * f - b) * f + c) * f + x0);
  }
  // First-order allpass over a tap of this line (stmlib DelayLine::Allpass).
  float allpass(float in, std::size_t delay, float coefficient) {
    const float r = read(delay);
    const float w = in + coefficient * r;
    write(w);
    return r - coefficient * w;
  }
  // RT-safe bounded copy for hot-reload state migration.
  void copy_from(const DelayLine& o) {
    const std::size_t n = std::min(buf_.size(), o.buf_.size());
    std::copy_n(o.buf_.begin(), n, buf_.begin());
    write_ = o.write_ & mask_;
  }

 private:
  std::vector<float> buf_;
  std::size_t mask_ = 0;
  std::size_t write_ = 0;
};

// 3-tap symmetric FIR lowpass with linear per-sample coefficient
// interpolation: y = gain * (h0*x[n-1] + h1*(x[n] + x[n-2])),
// h0 = (1+b)/2, h1 = (1-b)/4 (Rings DampingFilter).
class DampingFir {
 public:
  void reset() {
    x1_ = x2_ = 0.0f;
    gh0_ = gh1_ = gh0_step_ = gh1_step_ = 0.0f;
  }
  void configure(float gain, float brightness, int n) {
    const float h0 = (1.0f + brightness) * 0.5f;
    const float h1 = (1.0f - brightness) * 0.25f;
    const float inv = 1.0f / static_cast<float>(n);
    gh0_step_ = (gain * h0 - gh0_) * inv;
    gh1_step_ = (gain * h1 - gh1_) * inv;
  }
  float process(float x) {
    gh0_ += gh0_step_;
    gh1_ += gh1_step_;
    const float y = gh0_ * x1_ + gh1_ * (x + x2_);
    x2_ = x1_;
    x1_ = x;
    return y;
  }

 private:
  float gh0_ = 0.0f, gh1_ = 0.0f, gh0_step_ = 0.0f, gh1_step_ = 0.0f;
  float x1_ = 0.0f, x2_ = 0.0f;
};

// One-pole DC blocker: y = x - x' + pole*y'.
class DcBlocker {
 public:
  void init(float pole) {
    pole_ = pole;
    x_ = y_ = 0.0f;
  }
  float process(float in) {
    y_ = in - x_ + pole_ * y_;
    x_ = in;
    if (std::abs(y_) < 1e-20f) y_ = 0.0f;
    return y_;
  }

 private:
  float pole_ = 0.995f, x_ = 0.0f, y_ = 0.0f;
};

// Marsaglia xorshift32; see docs/attribution.md. Zero state is forbidden.
class Rng {
 public:
  explicit Rng(uint32_t seed = 1u) : s_(seed ? seed : 1u) {}
  void seed(uint32_t s) { s_ = s ? s : 1u; }
  float bipolar() {
    return static_cast<float>(xorshift32(s_)) * (2.0f / 4294967296.0f) - 1.0f;
  }

 private:
  uint32_t s_;
};

// Noise-burst pluck exciter (Rings Plucker): a period-scaled white-noise
// burst through a feedback comb and a lowpass. f/cutoff normalized (f/fs).
class Plucker {
 public:
  void init(uint32_t seed) {
    comb_.init(256);
    svf_.reset();
    rng_.seed(seed);
    remaining_ = 0;
    comb_period_ = 0.0f;
    comb_gain_ = 0.0f;
  }
  void trigger(float f, float cutoff, float position) {
    const float ratio = position * 0.9f + 0.05f;
    float period = ratio / f;
    remaining_ = static_cast<int>(period);
    while (period >= 255.0f) period *= 0.5f;
    comb_period_ = period;
    comb_gain_ = (1.0f - position) * 0.8f;
    svf_.set_f_q_fast(std::min(cutoff, 0.499f), 1.0f);
  }
  float process() {
    float in = 0.0f;
    if (remaining_ > 0) {
      in = rng_.bipolar();
      --remaining_;
    }
    const float s = in + comb_gain_ * comb_.read_linear(comb_period_);
    comb_.write(s);
    return svf_.process_lp(s);
  }
  void copy_from(const Plucker& o) {
    comb_.copy_from(o.comb_);
    svf_ = o.svf_;
    rng_ = o.rng_;
    remaining_ = o.remaining_;
    comb_period_ = o.comb_period_;
    comb_gain_ = o.comb_gain_;
  }

 private:
  DelayLine comb_;
  Svf svf_;
  Rng rng_{1u};
  int remaining_ = 0;
  float comb_period_ = 0.0f;
  float comb_gain_ = 0.0f;
};

}  // namespace ms::dsp::resonate
