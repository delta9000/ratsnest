#include <doctest/doctest.h>
#include <cmath>
#include <numbers>
#include <vector>
#include "dsp/resonate.h"

using namespace ms::dsp::resonate;

TEST_CASE("resonate: semitones_to_ratio matches 2^(x/12)") {
  CHECK(semitones_to_ratio(12.0f) == doctest::Approx(2.0f));
  CHECK(semitones_to_ratio(-12.0f) == doctest::Approx(0.5f));
  CHECK(semitones_to_ratio(7.01955f) == doctest::Approx(1.5f).epsilon(1e-4));
}

TEST_CASE("resonate: stiffness curve endpoints and plateau") {
  CHECK(stiffness_curve(0.0f) == doctest::Approx(-0.0625f));
  CHECK(stiffness_curve(0.25f) == doctest::Approx(0.0f));
  CHECK(stiffness_curve(0.29f) == doctest::Approx(0.0f));
  CHECK(stiffness_curve(0.9f) == doctest::Approx(1.0f).epsilon(0.02));
  CHECK(stiffness_curve(1.0f) == doctest::Approx(2.0f).epsilon(0.02));
  // Monotone rise across the exponential segment.
  CHECK(stiffness_curve(0.6f) > stiffness_curve(0.4f));
}

TEST_CASE("resonate: dispersion mapping is bipolar with a dead zone") {
  CHECK(dispersion_from_structure(0.0f) == doctest::Approx(-0.99984f));
  CHECK(dispersion_from_structure(0.24f) == doctest::Approx(0.0f));
  CHECK(dispersion_from_structure(0.25f) == doctest::Approx(0.0f));
  CHECK(dispersion_from_structure(0.26f) == doctest::Approx(0.0f));
  CHECK(dispersion_from_structure(0.9995f) ==
        doctest::Approx(0.9993f).epsilon(1e-3));
}

TEST_CASE("resonate: fast tan approximation tracks tan(pi f) in the audio band") {
  // The 5th-order fit is within ~1% up to ~13 kHz; above that it is
  // deliberately loose but must stay monotone and finite (hostile-cutoff
  // stability), so check accuracy in-band and shape across the whole range.
  for (float hz = 16.0f; hz < 13000.0f; hz *= 1.5f) {
    const float f = hz / 48000.0f;
    const float exact = std::tan(std::numbers::pi_v<float> * f);
    CHECK(Svf::tan_fast(f) == doctest::Approx(exact).epsilon(0.01));
  }
  float prev = Svf::tan_fast(0.0f);
  for (float f = 0.001f; f <= 0.499f; f += 0.001f) {
    const float g = Svf::tan_fast(f);
    CHECK(std::isfinite(g));
    CHECK(g > prev);  // strictly increasing
    prev = g;
  }
}

TEST_CASE("resonate: svf bandpass selects its center frequency") {
  auto rms_at = [](float tone_hz) {
    Svf svf;
    svf.reset();
    svf.set_f_q_fast(440.0f / 48000.0f, 20.0f);
    double sum = 0.0;
    for (int i = 0; i < 48000; ++i) {
      const float x = std::sin(2.0f * std::numbers::pi_v<float> * tone_hz *
                               static_cast<float>(i) / 48000.0f);
      const float y = svf.process_bp(x);
      if (i > 4800) sum += static_cast<double>(y) * y;
    }
    return std::sqrt(sum / (48000 - 4800));
  };
  CHECK(rms_at(440.0f) > 4.0 * rms_at(1760.0f));
}

TEST_CASE("resonate: cosine series: first weight is 1, exact at special positions") {
  CosineSeries at_zero(0.0f);
  for (int n = 0; n < 8; ++n)
    CHECK(at_zero.next() == doctest::Approx(1.0f).epsilon(1e-3));

  // position 0.5: w_n = 0.5 + 0.5*cos(pi n) -> 1, 0, 1, 0, ...
  CosineSeries at_half(0.5f);
  CHECK(at_half.next() == doctest::Approx(1.0f));
  CHECK(at_half.next() == doctest::Approx(0.0f).epsilon(1e-4));
  CHECK(at_half.next() == doctest::Approx(1.0f).epsilon(1e-4));

  // Approximate elsewhere: the parabolic recurrence drifts in phase, so it
  // tracks the true series to within ~0.2 for n <= 8 (position weighting is a
  // soft comb, exactness not required).
  CosineSeries approx(0.1f);
  for (int n = 0; n < 8; ++n) {
    const float exact =
        0.5f + 0.5f * std::cos(2.0f * std::numbers::pi_v<float> * 0.1f *
                               static_cast<float>(n));
    CHECK(std::abs(approx.next() - exact) < 0.2f);
  }
}

TEST_CASE("resonate: delay line reads back written history") {
  DelayLine d;
  d.init(64);
  for (int i = 0; i < 100; ++i) d.write(static_cast<float>(i));
  // read(k) is the sample written k writes ago (read(0) = most recent).
  CHECK(d.read(0) == doctest::Approx(99.0f));
  CHECK(d.read(10) == doctest::Approx(89.0f));
  CHECK(d.read_linear(10.5f) == doctest::Approx(88.5f));
  CHECK(d.read_hermite(10.0f) == doctest::Approx(89.0f).epsilon(1e-4));
}

TEST_CASE("resonate: damping fir at brightness 1 gain 1 is a pure one-sample delay") {
  DampingFir fir;
  fir.reset();
  float prev = 0.0f;
  for (int i = 1; i <= 16; ++i) {
    // Coefficients are re-targeted every block in the module; reconfigure per
    // sample here so the ramp holds at its target instead of overshooting.
    fir.configure(1.0f, 1.0f, 1);
    const float x = static_cast<float>(i);
    const float y = fir.process(x);
    if (i > 1) CHECK(y == doctest::Approx(prev));
    prev = x;
  }
}

TEST_CASE("resonate: dc blocker removes a constant input") {
  DcBlocker dc;
  dc.init(1.0f - 10.0f / 48000.0f);
  float y = 1.0f;
  for (int i = 0; i < 48000; ++i) y = dc.process(1.0f);
  CHECK(std::abs(y) < 1e-3f);
}

TEST_CASE("resonate: plucker is seed-deterministic") {
  auto run = [](uint32_t seed) {
    Plucker p;
    p.init(seed);
    p.trigger(220.0f / 48000.0f, 0.1f, 0.3f);
    std::vector<float> out(512);
    for (auto& s : out) s = p.process();
    return out;
  };
  const auto a = run(7), b = run(7), c = run(8);
  CHECK(a == b);
  CHECK(a != c);
  // The burst actually contains energy.
  double sum = 0.0;
  for (float s : a) sum += static_cast<double>(s) * s;
  CHECK(sum > 0.0);
}
