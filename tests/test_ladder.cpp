#include <doctest/doctest.h>
#include <algorithm>
#include <cmath>
#include <numbers>
#include "dsp/modules.h"
#include "filter_probe.h"

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
  float ins[5] = {0.0f, cutoff, res, 0.0f};
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
  const float ins[5] = {0.5f, 1000.0f, 0.0f, 0.0f};
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
  float ins[5] = {0.0f, 0.0f, 0.0f, 0.0f};
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

namespace {
filter_probe::Step ladder_step(Module& m, float cutoff, float res, float v_oct = 0.0f) {
  return [&m, cutoff, res, v_oct](float x, int) {
    const float ins[5] = {x, cutoff, res, 0.0f, v_oct};
    float out[1];
    m.process(ins, out);
    return out[0];
  };
}
}  // namespace

TEST_CASE("ladder stays bounded across the res range with a hot input") {
  for (int step = 0; step <= 10; ++step) {
    const float res = static_cast<float>(step) / 10.0f;
    CAPTURE(res);
    for (float amp : {0.8f, 4.0f}) {
      auto m = make_ladder();
      float peak = 0.0f;
      for (int i = 0; i < 48000; ++i) {
        const float x = amp * std::sin(static_cast<float>(2.0 * std::numbers::pi * 1000.0 * i / 48000.0));
        const float ins[5] = {x, 1000.0f, res, 0.0f};
        float out[1];
        m->process(ins, out);
        peak = std::max(peak, std::abs(out[0]));
      }
      CAPTURE(amp);
      CHECK(peak < 2.0f + 1.25f * amp);  // was ~890 at res 1 for a 0.8 sine
    }
  }
}

TEST_CASE("ladder self-oscillates at the cutoff at res 1") {
  struct Case { float cutoff, v_oct; };
  for (Case c : {Case{55.0f, 0.0f}, Case{440.0f, 0.0f}, Case{3520.0f, 0.0f}, Case{110.0f, 2.0f}}) {
    const double target = c.cutoff * std::exp2(c.v_oct);
    CAPTURE(target);
    auto m = make_ladder();
    const auto tone = filter_probe::ring(ladder_step(*m, c.cutoff, 1.0f, c.v_oct), 48000.0);
    CHECK(std::abs(filter_probe::cents(tone.hz, target)) < 3.0);
    CHECK(tone.peak == doctest::Approx(1.0).epsilon(0.1));
  }
}

TEST_CASE("ladder rings out below the self-oscillation edge") {
  auto m = make_ladder();
  const auto tone = filter_probe::ring(ladder_step(*m, 440.0f, 0.9f), 48000.0);
  CHECK(tone.peak < 1e-3);
}

TEST_CASE("ladder compensates part of the passband loss at full res") {
  // Linear ladder: DC gain 1/(1+k), -13 dB at res 0.9 and -15 dB at res 1.
  // The input gain 1 + 0.6*k*res^2 leaves about -4.5 dB below the oscillation
  // edge; at res 1 the saturated oscillation relaxes the feedback further
  // (documented in docs/modules.md).
  auto below = make_ladder();
  const double db =
      20.0 * std::log10(filter_probe::sine_gain(ladder_step(*below, 2000.0f, 0.9f), 48000.0, 100.0, 0.02f));
  CHECK(db > -5.0);
  CHECK(db < -3.0);
  auto top = make_ladder();
  const double db_top =
      20.0 * std::log10(filter_probe::sine_gain(ladder_step(*top, 2000.0f, 1.0f), 48000.0, 100.0, 0.25f));
  CHECK(db_top > -5.0);
  CHECK(db_top < 0.0);
  auto flat = make_ladder();
  CHECK(filter_probe::sine_gain(ladder_step(*flat, 2000.0f, 0.0f), 48000.0, 100.0, 0.25f) ==
        doctest::Approx(1.0).epsilon(0.01));
}

TEST_CASE("ladder stays bounded under fast cutoff modulation at full res") {
  auto m = make_ladder();
  float ins[5] = {0.0f, 0.0f, 1.0f, 0.0f};
  float out[1];
  for (int i = 0; i < 96000; ++i) {
    ins[0] = std::fmod(static_cast<float>(i) * 55.0f / 48000.0f, 1.0f) * 2.0f - 1.0f;
    ins[1] = 4000.0f + 3900.0f * std::sin(static_cast<float>(2.0 * std::numbers::pi * 1500.0 * i / 48000.0));
    m->process(ins, out);
    REQUIRE(std::abs(out[0]) < 6.0f);
  }
}
