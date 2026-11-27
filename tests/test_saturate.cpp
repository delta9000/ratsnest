#include <doctest/doctest.h>
#include <cmath>
#include <numbers>
#include <vector>
#include "dsp/modules.h"
#include "thd.h"

using namespace ms::dsp;

namespace {
// in_l, in_r, drive, bias, mix. Returns the left output for a 100 Hz sine.
std::vector<float> run(float amp, float drive, float bias, float mix = 1.0f) {
  const ModuleInfo* info = find_module("saturate");
  REQUIRE(info != nullptr);
  auto m = info->create();
  m->init(48000.0, {});
  float ins[5] = {0.0f, 0.0f, drive, bias, mix}, out[2];
  std::vector<float> o;
  for (int n = 0; n < 48000; ++n) {
    ins[0] = ins[1] = amp * std::sin(2.0f * std::numbers::pi_v<float> * 100.0f *
                                     static_cast<float>(n) / 48000.0f);
    m->process(ins, out);
    if (n >= 24000) o.push_back(out[0]);
  }
  return o;
}
double rms(const std::vector<float>& v) {
  double s = 0.0;
  for (float x : v) s += static_cast<double>(x) * x;
  return std::sqrt(s / static_cast<double>(v.size()));
}
// Magnitude of harmonic k of 100 Hz relative to the fundamental.
double harmonic(const std::vector<float>& x, int k) {
  auto mag = [&](double f) {
    double re = 0.0, im = 0.0;
    for (size_t n = 0; n < x.size(); ++n) {
      const double ph = 2.0 * std::numbers::pi * f * static_cast<double>(n) / 48000.0;
      re += x[n] * std::cos(ph);
      im += x[n] * std::sin(ph);
    }
    return std::hypot(re, im);
  };
  return mag(100.0 * k) / mag(100.0);
}
}  // namespace

TEST_CASE("saturate: small signals pass at unity, whatever the drive") {
  for (float drive : {0.0f, 12.0f, 24.0f}) {
    const auto o = run(0.001f, drive, 0.0f);
    CHECK(rms(o) == doctest::Approx(0.001 / std::sqrt(2.0)).epsilon(0.01));
  }
}

TEST_CASE("saturate: drive raises distortion and bends loud peaks down") {
  const auto gentle = run(0.5f, 0.0f, 0.0f), hot = run(0.5f, 18.0f, 0.0f);
  CHECK(thd(gentle, 100.0, 48000.0) < 0.03);
  CHECK(thd(hot, 100.0, 48000.0) > 0.15);
  float peak = 0.0f;
  for (float x : hot) peak = std::max(peak, std::abs(x));
  CHECK(peak < 0.25f);  // ~1/drive: the level where it bends
}

TEST_CASE("saturate: bias adds even harmonics, none without it") {
  const auto sym = run(0.5f, 12.0f, 0.0f), asym = run(0.5f, 12.0f, 0.6f);
  CHECK(harmonic(sym, 2) < 1e-3);
  CHECK(harmonic(asym, 2) > 0.02);
  double dc = 0.0;
  for (float x : asym) dc += x;
  CHECK(std::abs(dc / static_cast<double>(asym.size())) < 1e-3);  // DC blocked
}

TEST_CASE("saturate: mix 0 is the dry input, one sample late") {
  const ModuleInfo* info = find_module("saturate");
  auto m = info->create();
  m->init(48000.0, {});
  float ins[5] = {0.0f, 0.0f, 24.0f, 0.5f, 0.0f}, out[2];
  float prev = 0.0f;
  for (int n = 0; n < 1000; ++n) {
    ins[0] = std::sin(0.05f * static_cast<float>(n));
    ins[1] = -ins[0];
    m->process(ins, out);
    CHECK(out[0] == prev);
    prev = ins[0];
  }
}
