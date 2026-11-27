#pragma once
#include <algorithm>
#include <cmath>
#include <cstddef>
#include <numbers>
#include <vector>
#include "dsp/mastering.h"

namespace ms::dsp {

// One tape-style echo line, shared by `delay` and `echo`: a power-of-two ring
// read at a slewed fractional time with 4-point Hermite interpolation, and a
// feedback path that darkens, blocks DC and tanh-saturates what it writes back.
// init() allocates (control thread); everything else is RT-safe.
class EchoLine {
 public:
  static constexpr float kMaxSeconds = 10.0f;

  void init(double rate) {
    rate_ = static_cast<float>(rate);
    const auto need = static_cast<std::size_t>(kMaxSeconds * rate) + 4;
    std::size_t size = 1;
    while (size < need) size <<= 1;
    buf_.assign(size, 0.0f);
    mask_ = size - 1;
    write_ = 0;
    delay_ = -1.0;  // sentinel: snap to the target on the first sample
    lp_ = dc_x_ = dc_y_ = 0.0f;
  }

  // Longest usable delay in samples.
  double max_samples() const { return static_cast<double>(buf_.size() - 4); }

  // Glides toward `target` samples with one-pole coefficient `a`, then reads.
  float read(double target, double a) {
    if (delay_ < 0.0) delay_ = target;
    delay_ += (target - delay_) * a;
    // 4-point cubic Hermite read: f interpolates tap di toward tap di+1.
    const auto di = static_cast<std::size_t>(delay_);
    const auto f = static_cast<float>(delay_ - static_cast<double>(di));
    const std::size_t w = write_;
    const float x0 = buf_[(w - (di - 1)) & mask_];
    const float x1 = buf_[(w - di) & mask_];
    const float x2 = buf_[(w - (di + 1)) & mask_];
    const float x3 = buf_[(w - (di + 2)) & mask_];
    const float c1 = 0.5f * (x2 - x0);
    const float c2 = x0 - 2.5f * x1 + 2.0f * x2 - 0.5f * x3;
    const float c3 = 0.5f * (x3 - x0) + 1.5f * (x1 - x2);
    return ((c3 * f + c2) * f + c1) * f + x1;
  }

  // Writes `in` plus the darkened (one-pole gain `g`), DC-blocked and
  // saturated feedback `fb` x `wet`.
  void write(float in, float wet, float g, float fb) {
    lp_ += g * (wet - lp_);
    const float rpole = 1.0f - 2.0f * std::numbers::pi_v<float> * 10.0f / rate_;
    const float dc = lp_ - dc_x_ + rpole * dc_y_;
    dc_x_ = lp_;
    dc_y_ = dc;
    // The offline render path has no FTZ; flush decaying states ourselves so
    // render and play behave identically. dc_x_ (the DC blocker's one-sample
    // input memory) must flush alongside lp_/dc_y_: left unflushed it lags
    // lp_'s flush by a sample and re-seeds `dc` with a stale sub-threshold
    // value every cycle, sustaining a permanent +-1-ULP denormal limit cycle.
    if (std::abs(lp_) < 1e-20f) lp_ = 0.0f;
    if (std::abs(dc_x_) < 1e-20f) dc_x_ = 0.0f;
    if (std::abs(dc_y_) < 1e-20f) dc_y_ = 0.0f;
    const float sat = fb * dc;  // tanh(+-0) is +-0: skip it on a silent loop
    // Never store a non-finite sample: it would replay for up to 10 s.
    buf_[write_] = sanitize(in + (sat == 0.0f ? sat : std::tanh(sat)));
    write_ = (write_ + 1) & mask_;
  }

  // RT-called on hot reload: bounded copy, no allocation. Sizes match in
  // practice (same rate); min() is defense if they ever don't.
  void copy_from(const EchoLine& o) {
    const std::size_t n = std::min(buf_.size(), o.buf_.size());
    std::copy_n(o.buf_.begin(), n, buf_.begin());
    write_ = o.write_ & mask_;
    delay_ = std::min(o.delay_, max_samples());
    lp_ = o.lp_;
    dc_x_ = o.dc_x_;
    dc_y_ = o.dc_y_;
  }

 private:
  float rate_ = 48000.0f;
  std::vector<float> buf_;
  std::size_t mask_ = 0, write_ = 0;
  double delay_ = -1.0;
  float lp_ = 0.0f, dc_x_ = 0.0f, dc_y_ = 0.0f;
};

// Delay time in samples from seconds and a v/oct pitch, clamped to [2, max].
// NaN-proof lower clamp: 0 * inf from a hostile pitch input must not poison
// the line forever. !(x >= 2) catches NaN as well as small values.
inline double echo_samples(float time, float pitch, float rate, double max) {
  const float secs = std::clamp(time, 0.0f, EchoLine::kMaxSeconds) * std::exp2(-pitch);
  double target = static_cast<double>(secs) * static_cast<double>(rate);
  if (!(target >= 2.0)) target = 2.0;
  return std::min(target, max);
}

// One-pole glide coefficient for a slew time in seconds.
inline double echo_slew(float slew, float rate) {
  const float slew_samples = std::max(sanitize(slew), 0.0f) * rate;
  return slew_samples <= 1.0f ? 1.0 : 1.0 - std::exp(-1.0 / static_cast<double>(slew_samples));
}

// Feedback-tone one-pole gain for a cutoff in Hz.
inline float echo_tone(float hz, float rate) {
  const float fc = std::clamp(sanitize(hz), 20.0f, 0.45f * rate);
  return 1.0f - std::exp(-2.0f * std::numbers::pi_v<float> * fc / rate);
}

}  // namespace ms::dsp
