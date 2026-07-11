#include <doctest/doctest.h>
#include <cmath>
#include <numbers>
#include "dsp/modules.h"

using namespace ms::dsp;

namespace {
std::unique_ptr<Module> make_ladder() {
  const ModuleInfo* info = find_module("ladder");
  REQUIRE(info != nullptr);
  auto m = info->create();
  m->init(48000.0, {});
  return m;
}

double ladder_rms(float frequency, float cutoff, float res) {
  auto m = make_ladder();
  float ins[4] = {0.0f, cutoff, res, 0.0f};
  float out[1];
  double sum = 0.0;
  constexpr int warmup = 4096;
  constexpr int count = 48000;
  for (int i = 0; i < warmup + count; ++i) {
    ins[0] = 0.5f * std::sin(static_cast<float>(
        2.0 * std::numbers::pi * frequency * i / 48000.0));
    m->process(ins, out);
    if (i >= warmup) sum += static_cast<double>(out[0]) * out[0];
  }
  return std::sqrt(sum / count);
}
}  // namespace

TEST_CASE("ladder settles to DC with no resonance") {
  auto m = make_ladder();
  const float ins[4] = {0.5f, 1000.0f, 0.0f, 0.0f};
  float out[1];
  for (int i = 0; i < 48000; ++i) m->process(ins, out);
  CHECK(out[0] == doctest::Approx(0.5f).epsilon(0.01));
}

TEST_CASE("ladder rolls off high frequencies steeply") {
  const double low = ladder_rms(100.0f, 1000.0f, 0.0f);
  const double high = ladder_rms(10000.0f, 1000.0f, 0.0f);
  CHECK(low > 0.3);
  CHECK(high < low * 0.001);  // ~24 dB/oct: >60 dB down at 10x cutoff
}

TEST_CASE("ladder resonance boosts near the cutoff") {
  const double flat = ladder_rms(1000.0f, 1000.0f, 0.0f);
  const double peaked = ladder_rms(1000.0f, 1000.0f, 0.85f);
  CHECK(peaked > flat * 1.5);
}

TEST_CASE("ladder stays finite under extreme modulation") {
  auto m = make_ladder();
  float ins[4] = {0.0f, 0.0f, 0.0f, 0.0f};
  float out[1];
  for (int i = 0; i < 100000; ++i) {
    ins[0] = i % 2 == 0 ? 1.0f : -1.0f;
    ins[1] = i % 3 == 0 ? -500.0f : 900000.0f;
    ins[2] = i % 5 == 0 ? -4.0f : 4.0f;
    ins[3] = i % 7 == 0 ? 10.0f : 0.0f;
    m->process(ins, out);
    if (!std::isfinite(out[0]) || std::abs(out[0]) >= 100.0f) {
      CAPTURE(i);
      REQUIRE(false);
    }
  }
}
