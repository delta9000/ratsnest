#pragma once
#include <cmath>
#include <algorithm>

namespace ms::dsp {

// First-order allpass filter section:
// H(z) = (a + z^-1) / (1 + a * z^-1)
// y[n] = a * x[n] + s[n-1]
// s[n] = x[n] - a * y[n]
struct Allpass {
  float s = 0.0f;
  
  void clear() {
    s = 0.0f;
  }
  
  inline float process(float x, float a) {
    float y = a * x + s;
    s = x - a * y;
    if (std::abs(s) < 1e-20f) s = 0.0f; // Flush denormals
    return y;
  }
};

// 2-path polyphase IIR half-band filter for 2x oversampling.
// stopband attenuation ~40 dB with coefficients:
// Path 0: a0 = 0.117565365538f
// Path 1: a1 = 0.541450201656f
// The design provenance of the two coefficients below was not recorded when
// this file was introduced and could not be recovered during the 2026-07-14
// attribution audit. Do not attribute them speculatively; see the unresolved
// entry in docs/attribution.md before replacing or regenerating them.
struct Halfband2x {
  Allpass ap0;
  Allpass ap1;
  float w1_prev = 0.0f;
  
  void clear() {
    ap0.clear();
    ap1.clear();
    w1_prev = 0.0f;
  }
  
  // Interpolate: 1 sample at 1x -> 2 samples at 2x
  inline void interpolate(float x, float& y0, float& y1) {
    y0 = ap0.process(x, 0.117565365538f);
    y1 = ap1.process(x, 0.541450201656f);
  }
  
  // Decimate: 2 samples at 2x -> 1 sample at 1x
  inline float decimate(float w0, float w1) {
    float v0 = ap0.process(w0, 0.117565365538f);
    float v1 = ap1.process(w1_prev, 0.541450201656f);
    w1_prev = w1;
    return 0.5f * (v0 + v1);
  }
};

// 4x oversampler by cascading two 2x stages
struct Oversampler4x {
  Halfband2x stage1;
  Halfband2x stage2a;
  Halfband2x stage2b;
  
  void clear() {
    stage1.clear();
    stage2a.clear();
    stage2b.clear();
  }
  
  inline void upsample(float x, float* y) {
    float w0, w1;
    stage1.interpolate(x, w0, w1);
    stage2a.interpolate(w0, y[0], y[1]);
    stage2b.interpolate(w1, y[2], y[3]);
  }
};

// 4x decimator by cascading two 2x stages
struct Decimator4x {
  Halfband2x stage1a;
  Halfband2x stage1b;
  Halfband2x stage2;
  
  void clear() {
    stage1a.clear();
    stage1b.clear();
    stage2.clear();
  }
  
  inline float downsample(const float* x) {
    float w0 = stage1a.decimate(x[0], x[1]);
    float w1 = stage1b.decimate(x[2], x[3]);
    return stage2.decimate(w0, w1);
  }
};

} // namespace ms::dsp
