#include <doctest/doctest.h>
#include <algorithm>
#include <cmath>
#include <numbers>
#include "dsp/waveshape.h"
#include "dsp/biquad.h"

using namespace ms::dsp::waveshape;

TEST_CASE("waveshape: curves match their definitions") {
  CHECK(f_eval(Curve::Hard, 0.5f) == doctest::Approx(0.5f));
  CHECK(f_eval(Curve::Hard, 2.0f) == doctest::Approx(1.0f));
  CHECK(f_eval(Curve::Hard, -2.0f) == doctest::Approx(-1.0f));
  CHECK(f_eval(Curve::Soft, 0.0f) == doctest::Approx(0.0f));
  CHECK(f_eval(Curve::Soft, 1.0f) == doctest::Approx(1.0f / std::sqrt(2.0f)));
  CHECK(f_eval(Curve::Soft, 1e9f) == doctest::Approx(1.0f).epsilon(1e-3));  // saturates
  CHECK(f_eval(Curve::Cubic, 0.0f) == doctest::Approx(0.0f));
  CHECK(f_eval(Curve::Cubic, 2.0f) == doctest::Approx(1.0f));
  CHECK(f_eval(Curve::Cubic, -2.0f) == doctest::Approx(-1.0f));
  // odd symmetry
  CHECK(f_eval(Curve::Soft, -0.7f) == doctest::Approx(-f_eval(Curve::Soft, 0.7f)));
}

TEST_CASE("waveshape: antiderivative matches numeric integral of the curve") {
  for (Curve c : {Curve::Hard, Curve::Soft, Curve::Cubic}) {
    // Trapezoidal integral of f from 0 to X, vs F(X)-F(0).
    const float X = 1.7f;
    const int N = 20000;
    double integral = 0.0;
    float prev = f_eval(c, 0.0f);
    for (int i = 1; i <= N; ++i) {
      const float x = X * static_cast<float>(i) / N;
      const float cur = f_eval(c, x);
      integral += 0.5 * (prev + cur) * (X / N);
      prev = cur;
    }
    const double analytic = f_anti(c, X) - f_anti(c, 0.0f);
    CHECK(analytic == doctest::Approx(integral).epsilon(1e-3));
  }
}

TEST_CASE("waveshape: ADAA steady state equals the biased curve") {
  // Constant input -> dx=0 branch -> returns g(x) = f(x+b)-f(b).
  Shaper s;
  s.reset();
  s.configure(Curve::Soft, 0.1f);
  const float x = 0.6f;
  s.process(x);            // primes x1_
  const float y = s.process(x);
  const float g = f_eval(Curve::Soft, x + 0.1f) - f_eval(Curve::Soft, 0.1f);
  CHECK(y == doctest::Approx(g).epsilon(1e-5));
}

TEST_CASE("waveshape: ADAA is finite and bounded on a swept input") {
  Shaper s;
  s.reset();
  s.configure(Curve::Hard, 0.0f);
  float maxabs = 0.0f;
  for (int i = 0; i < 4000; ++i) {
    const float x = 5.0f * std::sin(0.03f * static_cast<float>(i));
    const float y = s.process(x);
    CHECK(std::isfinite(y));
    maxabs = std::max(maxabs, std::abs(y));
  }
  CHECK(maxabs <= 1.001f);  // hard clip bound survives ADAA
}

TEST_CASE("waveshape: make_lpf passes DC and attenuates high frequencies") {
  const float sr = 48000.0f;
  ms::dsp::BiquadCoeffs c = ms::dsp::make_lpf(1000.0f, sr);
  auto rms = [&](float hz) {
    ms::dsp::BiquadState st;
    st.clear();
    double sum = 0.0;
    for (int i = 0; i < 48000; ++i) {
      const float x = std::sin(2.0f * std::numbers::pi_v<float> * hz *
                               static_cast<float>(i) / sr);
      const float y = ms::dsp::Biquad::process(x, c, st);
      if (i > 4800) sum += static_cast<double>(y) * y;
    }
    return std::sqrt(sum / (48000 - 4800));
  };
  CHECK(rms(100.0f) > 0.6);        // near unity in-band
  CHECK(rms(12000.0f) < 0.1);      // rolled off well above corner
}
