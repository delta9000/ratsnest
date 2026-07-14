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
