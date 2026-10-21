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
