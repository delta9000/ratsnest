#pragma once
#include <cmath>
#include <algorithm>

namespace ms::dsp {

// Helper to flush denormals below 1e-20
inline float flush_denormal(float val) {
  return (std::abs(val) < 1e-20f) ? 0.0f : val;
}

// Standard one-pole parameter smoothing filter
class OnePole {
 public:
  void set_time(float time_seconds, float sample_rate) {
    if (time_seconds <= 0.0f) {
      coeff_ = 1.0f;
    } else {
      coeff_ = 1.0f - std::exp(-1.0f / (time_seconds * sample_rate));
    }
  }
  
  void init(float initial_value) {
    value_ = initial_value;
  }
  
  float process(float target) {
    value_ += (target - value_) * coeff_;
    value_ = flush_denormal(value_);
    return value_;
  }
  
  float value() const { return value_; }
  
 private:
  float coeff_ = 0.0f;
  float value_ = 0.0f;
};

// Maximum of the last `len` pushed values (clamped 1..N), O(1) amortised: a
// monotonic deque after Lemire, arXiv:cs/0610046. N is a power of two. The
// window may change length between pushes. See docs/attribution.md.
template <unsigned N>
class SlidingMax {
  static_assert((N & (N - 1)) == 0);

 public:
  float push(float v, int len) {
    const unsigned span = static_cast<unsigned>(std::clamp(len, 1, static_cast<int>(N)));
    ++now_;
    while (tail_ != head_ && val_[(tail_ - 1) & (N - 1)] <= v) --tail_;
    val_[tail_ & (N - 1)] = v;
    at_[tail_++ & (N - 1)] = now_;
    while (now_ - at_[head_ & (N - 1)] >= span) ++head_;
    return val_[head_ & (N - 1)];
  }

 private:
  float val_[N] = {};
  unsigned at_[N] = {};
  unsigned head_ = 0, tail_ = 0, now_ = 0;
};

// Inter-sample peak estimate after ITU-R BS.1770-4 Annex 2: 4x polyphase
// interpolation, 12 taps per phase. The coefficients are generated locally
// (sinc times a Kaiser window, beta 5, each phase normalised to unity DC
// gain), not the standard's table; at fs/4 they read about 0.015 dB high.
// push(x[n]) returns the largest magnitude among x[n-6] and the three points
// between it and x[n-5], so it lags the input by kLag samples. See
// docs/attribution.md.
class TruePeak4x {
 public:
  static constexpr int kLag = 6;
  float push(float x) {
    std::copy(h_ + 1, h_ + 12, h_);
    h_[11] = x;
    float m = std::abs(h_[5]);
    for (const auto& ph : kPhases) {
      float y = 0.0f;
      for (int k = 0; k < 12; ++k) y += ph[k] * h_[k];
      m = std::max(m, std::abs(y));
    }
    return m;
  }

 private:
  static constexpr float kPhases[3][12] = {
      {-0.004879555f, 0.014719555f, -0.034300832f, 0.072401957f, -0.163447096f, 0.897775454f,
       0.290083687f, -0.106084276f, 0.050049278f, -0.022932814f, 0.008885806f, -0.002271165f},
      {-0.004843870f, 0.016329487f, -0.039836273f, 0.085150470f, -0.184445326f, 0.627645512f,
       0.627645512f, -0.184445326f, 0.085150470f, -0.039836273f, 0.016329487f, -0.004843870f},
      {-0.002271165f, 0.008885806f, -0.022932814f, 0.050049278f, -0.106084276f, 0.290083687f,
       0.897775454f, -0.163447096f, 0.072401957f, -0.034300832f, 0.014719555f, -0.004879555f}};
  float h_[12] = {};
};

// Dynamics peak-hold length: bridges the troughs of |x| for fundamentals down
// to 25 Hz (20 ms), so bass does not ripple the gain, but never outlasts the
// release the patch asked for.
inline int hold_samples(float release_s, float rate) {
  return static_cast<int>(std::min(release_s, 0.02f) * rate);
}

// dB and linear conversion helpers
inline float db_to_linear(float db) {
  return std::pow(10.0f, db / 20.0f);
}

inline float linear_to_db(float lin) {
  if (lin <= 1e-12f) return -240.0f; // clamp to a safe floor
  return 20.0f * std::log10(lin);
}

// Sanitize utility to prevent NaN/Inf propagation
inline float sanitize(float v, float fallback = 0.0f) {
  return std::isfinite(v) ? v : fallback;
}

inline float sanitize_clamp(float v, float min_val, float max_val, float fallback = 0.0f) {
  return std::isfinite(v) ? std::clamp(v, min_val, max_val) : fallback;
}

} // namespace ms::dsp
