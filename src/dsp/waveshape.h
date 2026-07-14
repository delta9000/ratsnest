#pragma once
// Memoryless waveshaper curves with antiderivatives, plus first-order
// antiderivative anti-aliasing (ADAA), for the overdrive module.
// Curves after Vicanek (vicanek.de/articles/AADistortion.pdf); ADAA after
// Parker, Zavalishin & Le Bivic (DAFx-16). Constants documented in
// docs/superpowers/specs/2026-07-14-overdrive-design.md.
#include <cmath>

namespace ms::dsp::waveshape {

enum class Curve { Hard, Soft, Cubic };

// Raw curve f(x).
inline float f_eval(Curve c, float x) {
  switch (c) {
    case Curve::Hard:
      return x < -1.0f ? -1.0f : (x > 1.0f ? 1.0f : x);
    case Curve::Soft:
      return x / std::sqrt(1.0f + x * x);  // tanh-like, ideal ADAA
    case Curve::Cubic:
      if (x <= -1.0f) return -1.0f;
      if (x >= 1.0f) return 1.0f;
      return 1.5f * x - 0.5f * x * x * x;
  }
  return x;
}

// Antiderivative F(x) with F chosen continuous across the piecewise joins.
inline float f_anti(Curve c, float x) {
  switch (c) {
    case Curve::Hard: {
      const float a = std::abs(x);
      return a <= 1.0f ? 0.5f * x * x : a - 0.5f;
    }
    case Curve::Soft:
      return std::sqrt(1.0f + x * x);
    case Curve::Cubic: {
      if (x >= 1.0f) return x - 0.375f;    // continues linearly from F(1)=0.625
      if (x <= -1.0f) return -x - 0.375f;
      return 0.75f * x * x - 0.125f * x * x * x * x;
    }
  }
  return 0.0f;
}

// First-order ADAA waveshaper with an asymmetric bias b:
//   g(x)  = f(x+b) - f(b)          (odd curve shifted, DC removed downstream)
//   G(x)  = F(x+b) - f(b)*x        (its antiderivative)
//   y[n]  = (G(x[n]) - G(x[n-1])) / (x[n]-x[n-1]),  fallback g(mid) as dx->0.
class Shaper {
 public:
  void reset() { x1_ = 0.0f; }
  void configure(Curve c, float bias) {
    curve_ = c;
    bias_ = bias;
    fb_ = f_eval(c, bias);
  }
  float process(float x) {
    const float dx = x - x1_;
    float y;
    if (std::abs(dx) < 1.0e-4f) {  // ~sqrt(FLT_EPSILON): avoid cancellation
      const float m = 0.5f * (x + x1_);
      y = f_eval(curve_, m + bias_) - fb_;
    } else {
      const float g1 = f_anti(curve_, x + bias_) - fb_ * x;
      const float g0 = f_anti(curve_, x1_ + bias_) - fb_ * x1_;
      y = (g1 - g0) / dx;
    }
    x1_ = x;
    return y;
  }

 private:
  Curve curve_ = Curve::Soft;
  float bias_ = 0.0f;
  float fb_ = 0.0f;
  float x1_ = 0.0f;
};

}  // namespace ms::dsp::waveshape
