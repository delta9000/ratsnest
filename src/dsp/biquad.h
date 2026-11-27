#pragma once
#include <cmath>
#include <numbers>

namespace ms::dsp {

struct BiquadCoeffs {
  float b0 = 1.0f;
  float b1 = 0.0f;
  float b2 = 0.0f;
  float a1 = 0.0f;
  float a2 = 0.0f;
};

struct BiquadState {
  float s1 = 0.0f;
  float s2 = 0.0f;
  
  void clear() {
    s1 = 0.0f;
    s2 = 0.0f;
  }
};

class Biquad {
 public:
  static inline float process(float x, const BiquadCoeffs& c, BiquadState& s) {
    // Transposed Direct Form II difference equations:
    // y[n] = b0 * x[n] + s1[n-1]
    // s1[n] = b1 * x[n] - a1 * y[n] + s2[n-1]
    // s2[n] = b2 * x[n] - a2 * y[n]
    float y = c.b0 * x + s.s1;
    s.s1 = c.b1 * x - c.a1 * y + s.s2;
    s.s2 = c.b2 * x - c.a2 * y;
    
    // Flush denormals to prevent high CPU usage
    if (std::abs(s.s1) < 1e-20f) s.s1 = 0.0f;
    if (std::abs(s.s2) < 1e-20f) s.s2 = 0.0f;
    
    return y;
  }
};

// Bristow-Johnson/W3C Audio EQ Cookbook coefficients, technical-note-derived.
// See docs/attribution.md.

inline BiquadCoeffs make_bypass() {
  BiquadCoeffs c;
  c.b0 = 1.0f;
  c.b1 = 0.0f;
  c.b2 = 0.0f;
  c.a1 = 0.0f;
  c.a2 = 0.0f;
  return c;
}

inline BiquadCoeffs make_hpf(float freq, float sample_rate) {
  float w0 = 2.0f * std::numbers::pi_v<float> * freq / sample_rate;
  float cos_w0 = std::cos(w0);
  float alpha = std::sin(w0) / (2.0f * 0.70710678f); // Q = 0.7071 (Butterworth)
  
  float a0 = 1.0f + alpha;
  BiquadCoeffs c;
  c.b0 = ((1.0f + cos_w0) / 2.0f) / a0;
  c.b1 = (-(1.0f + cos_w0)) / a0;
  c.b2 = ((1.0f + cos_w0) / 2.0f) / a0;
  c.a1 = (-2.0f * cos_w0) / a0;
  c.a2 = (1.0f - alpha) / a0;
  return c;
}

inline BiquadCoeffs make_lpf(float freq, float sample_rate) {
  float w0 = 2.0f * std::numbers::pi_v<float> * freq / sample_rate;
  float cos_w0 = std::cos(w0);
  float alpha = std::sin(w0) / (2.0f * 0.70710678f); // Q = 0.7071 (Butterworth)

  float a0 = 1.0f + alpha;
  BiquadCoeffs c;
  c.b0 = ((1.0f - cos_w0) / 2.0f) / a0;
  c.b1 = (1.0f - cos_w0) / a0;
  c.b2 = ((1.0f - cos_w0) / 2.0f) / a0;
  c.a1 = (-2.0f * cos_w0) / a0;
  c.a2 = (1.0f - alpha) / a0;
  return c;
}

// Second-order allpass, Q = 0.7071: what a Linkwitz-Riley LP4 + HP4 pair
// at the same frequency sums to (the crossover's phase match).
inline BiquadCoeffs make_apf(float freq, float sample_rate) {
  const float w0 = 2.0f * std::numbers::pi_v<float> * freq / sample_rate;
  const float cos_w0 = std::cos(w0);
  const float alpha = std::sin(w0) / (2.0f * 0.70710678f);
  const float a0 = 1.0f + alpha;
  BiquadCoeffs c;
  c.b0 = (1.0f - alpha) / a0;
  c.b1 = (-2.0f * cos_w0) / a0;
  c.b2 = 1.0f;  // (1 + alpha) / a0 == 1 exactly
  c.a1 = c.b1;
  c.a2 = c.b0;
  return c;
}

inline BiquadCoeffs make_low_shelf(float freq, float gain_db, float sample_rate,
                                  float q = 1.0f) {
  if (std::abs(gain_db) < 1e-4f) {
    return make_bypass();
  }
  float w0 = 2.0f * std::numbers::pi_v<float> * freq / sample_rate;
  float cos_w0 = std::cos(w0);
  float A = std::pow(10.0f, gain_db / 40.0f);
  // beta = sqrt(A) / Q * sin(w0); Q = 1 (the default) is the original
  // shelf, with a small bump past the corner; 0.707 has none.
  float beta = std::sqrt(A) * std::sin(w0) / q;
  
  float a0 = (A + 1.0f) + (A - 1.0f) * cos_w0 + beta;
  BiquadCoeffs c;
  c.b0 = (A * ((A + 1.0f) - (A - 1.0f) * cos_w0 + beta)) / a0;
  c.b1 = (2.0f * A * ((A - 1.0f) - (A + 1.0f) * cos_w0)) / a0;
  c.b2 = (A * ((A + 1.0f) - (A - 1.0f) * cos_w0 - beta)) / a0;
  c.a1 = (-2.0f * ((A - 1.0f) + (A + 1.0f) * cos_w0)) / a0;
  c.a2 = ((A + 1.0f) + (A - 1.0f) * cos_w0 - beta) / a0;
  return c;
}

inline BiquadCoeffs make_high_shelf(float freq, float gain_db, float sample_rate,
                                  float q = 1.0f) {
  if (std::abs(gain_db) < 1e-4f) {
    return make_bypass();
  }
  float w0 = 2.0f * std::numbers::pi_v<float> * freq / sample_rate;
  float cos_w0 = std::cos(w0);
  float A = std::pow(10.0f, gain_db / 40.0f);
  // beta = sqrt(A) / Q * sin(w0); Q = 1 (the default) is the original
  // shelf, with a small bump past the corner; 0.707 has none.
  float beta = std::sqrt(A) * std::sin(w0) / q;
  
  float a0 = (A + 1.0f) - (A - 1.0f) * cos_w0 + beta;
  BiquadCoeffs c;
  c.b0 = (A * ((A + 1.0f) + (A - 1.0f) * cos_w0 + beta)) / a0;
  c.b1 = (-2.0f * A * ((A - 1.0f) + (A + 1.0f) * cos_w0)) / a0;
  c.b2 = (A * ((A + 1.0f) + (A - 1.0f) * cos_w0 - beta)) / a0;
  c.a1 = (2.0f * ((A - 1.0f) - (A + 1.0f) * cos_w0)) / a0;
  c.a2 = ((A + 1.0f) - (A - 1.0f) * cos_w0 - beta) / a0;
  return c;
}

inline BiquadCoeffs make_peaking_eq(float freq, float gain_db, float Q, float sample_rate) {
  if (std::abs(gain_db) < 1e-4f) {
    return make_bypass();
  }
  float w0 = 2.0f * std::numbers::pi_v<float> * freq / sample_rate;
  float cos_w0 = std::cos(w0);
  float alpha = std::sin(w0) / (2.0f * Q);
  float A = std::pow(10.0f, gain_db / 40.0f);
  
  float a0 = 1.0f + alpha / A;
  BiquadCoeffs c;
  c.b0 = (1.0f + alpha * A) / a0;
  c.b1 = (-2.0f * cos_w0) / a0;
  c.b2 = (1.0f - alpha * A) / a0;
  c.a1 = (-2.0f * cos_w0) / a0;
  c.a2 = (1.0f - alpha / A) / a0;
  return c;
}

} // namespace ms::dsp
