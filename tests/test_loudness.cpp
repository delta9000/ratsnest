#include <doctest/doctest.h>
#include <cmath>
#include <numbers>
#include <vector>
#include "io/loudness.h"

namespace {
constexpr double kRate = 48000.0;

// A stereo sine, identical on both channels, `db` dBFS peak, `phase` radians.
void sine(std::vector<float>& v, double hz, double db, double secs, double phase = 0.0) {
  const double a = std::pow(10.0, db / 20.0);
  const auto n = static_cast<size_t>(secs * kRate);
  for (size_t i = 0; i < n; ++i)
    v.push_back(static_cast<float>(a * std::sin(2.0 * std::numbers::pi * hz *
                                                static_cast<double>(i) / kRate + phase)));
}

ms::io::Loudness measure(const std::vector<float>& v) {
  return ms::io::measure_loudness(v.data(), v.data(), v.size(), kRate);
}
}  // namespace

TEST_CASE("loudness: EBU 3341 case 1, a -23 dBFS 1 kHz stereo sine reads -23 LUFS") {
  std::vector<float> v;
  sine(v, 1000.0, -23.0, 20.0);
  CHECK(measure(v).integrated == doctest::Approx(-23.0).epsilon(0.004));
}

TEST_CASE("loudness: K-weighting holds at other sample rates") {
  for (double rate : {44100.0, 96000.0}) {
    std::vector<float> v;
    const auto n = static_cast<size_t>(10.0 * rate);
    for (size_t i = 0; i < n; ++i)
      v.push_back(static_cast<float>(std::pow(10.0, -23.0 / 20.0) *
                                     std::sin(2.0 * std::numbers::pi * 1000.0 *
                                              static_cast<double>(i) / rate)));
    CHECK(ms::io::measure_loudness(v.data(), v.data(), n, rate).integrated ==
          doctest::Approx(-23.0).epsilon(0.004));
  }
}

TEST_CASE("loudness: gating ignores silence and the quiet tail") {
  std::vector<float> v(static_cast<size_t>(10.0 * kRate), 0.0f);  // 10 s silence
  sine(v, 1000.0, -20.0, 10.0);
  sine(v, 1000.0, -60.0, 10.0);  // more than 10 LU under: relative gate drops it
  // Within 0.2 LU: the 400 ms blocks straddling each edge are partly quiet.
  CHECK(std::abs(measure(v).integrated + 20.0) < 0.2);
}

TEST_CASE("loudness: EBU 3342 case 1, -20 then -30 LUFS reads a 10 LU range") {
  std::vector<float> v;
  sine(v, 1000.0, -20.0, 20.0);
  sine(v, 1000.0, -30.0, 20.0);
  CHECK(measure(v).range == doctest::Approx(10.0).epsilon(0.1));
}

TEST_CASE("loudness: a steady tone has no range") {
  std::vector<float> v;
  sine(v, 440.0, -18.0, 15.0);
  CHECK(measure(v).range < 0.1);
}

TEST_CASE("loudness: true peak sees the crest between samples") {
  // fs/4 at 45 degrees: every sample sits at 0.707 of the crest.
  std::vector<float> v;
  sine(v, kRate / 4.0, 0.0, 1.0, std::numbers::pi / 4.0);
  const auto lu = measure(v);
  CHECK(lu.true_peak > -0.3);
  CHECK(lu.true_peak < 0.3);
}

TEST_CASE("loudness: silence and a too-short take read -inf, no range") {
  std::vector<float> v(1000, 0.0f);
  const auto lu = measure(v);
  CHECK(std::isinf(lu.integrated));
  CHECK(lu.range == 0.0);
}
