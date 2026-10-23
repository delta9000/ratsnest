#include <doctest/doctest.h>
#include <algorithm>
#include <cmath>
#include <numbers>
#include <vector>
#include "dsp/modules.h"
#include "filter_probe.h"

using namespace ms::dsp;

namespace {
std::unique_ptr<Module> make_svf(double rate = 48000.0) {
  const ModuleInfo* info = find_module("svf");
  REQUIRE(info != nullptr);
  auto module = info->create();
  module->init(rate, {});
  return module;
}

double filtered_rms(float frequency, float cutoff) {
  constexpr double rate = 48000.0;
  auto module = make_svf(rate);
  float ins[4] = {0.0f, cutoff, 0.0f};
  float out[3];
  double sum = 0.0;
  constexpr int warmup = 4096;
  constexpr int count = 48000;
  for (int i = 0; i < warmup + count; ++i) {
    ins[0] = std::sin(static_cast<float>(2.0 * std::numbers::pi * frequency * i / rate));
    module->process(ins, out);
    if (i >= warmup) sum += static_cast<double>(out[0]) * out[0];
  }
  return std::sqrt(sum / count);
}
}  // namespace

TEST_CASE("svf exposes low band and high pass outputs") {
  const ModuleInfo* info = find_module("svf");
  REQUIRE(info != nullptr);
  CHECK(info->input_index("in") == 0);
  CHECK(info->input_index("cutoff") == 1);
  CHECK(info->input_index("res") == 2);
  CHECK(info->output_index("lp") == 0);
  CHECK(info->output_index("bp") == 1);
  CHECK(info->output_index("hp") == 2);
}

TEST_CASE("svf low pass settles to DC") {
  auto module = make_svf();
  const float ins[4] = {1.0f, 1000.0f, 0.0f};
  float out[3];
  for (int i = 0; i < 48000; ++i) module->process(ins, out);
  CHECK(out[0] == doctest::Approx(1.0f).epsilon(0.001));
  CHECK(std::abs(out[1]) < 0.001f);
  CHECK(std::abs(out[2]) < 0.001f);
}

TEST_CASE("svf low pass attenuates frequencies above cutoff") {
  const double low = filtered_rms(100.0f, 1000.0f);
  const double high = filtered_rms(10000.0f, 1000.0f);
  CHECK(low > 0.65);
  CHECK(high < 0.02);
  CHECK(high < low * 0.03);
}

TEST_CASE("svf outputs share one coherent filter state") {
  auto module = make_svf();
  float ins[4] = {0.37f, 2400.0f, 0.0f};
  float out[3];
  for (int i = 0; i < 100; ++i) module->process(ins, out);
  CHECK(out[0] + 2.0f * out[1] + out[2] == doctest::Approx(ins[0]).epsilon(0.0001));
}

TEST_CASE("svf stays finite under extreme audio-rate modulation") {
  auto module = make_svf();
  float ins[4] = {0.0f, 0.0f, 0.0f};
  float out[3];
  for (int i = 0; i < 200000; ++i) {
    ins[0] = i % 2 == 0 ? 1.0f : -1.0f;
    ins[1] = i % 3 == 0 ? -1000.0f : 1000000.0f;
    ins[2] = i % 5 == 0 ? -10.0f : 10.0f;
    module->process(ins, out);
    for (float value : out) {
      REQUIRE(std::isfinite(value));
      REQUIRE(std::abs(value) < 100.0f);
    }
  }
}

namespace {
// Step function for svf output `port` at fixed cutoff/res/v_oct.
filter_probe::Step svf_step(Module& m, int port, float cutoff, float res, float v_oct = 0.0f) {
  return [&m, port, cutoff, res, v_oct](float x, int) {
    const float ins[4] = {x, cutoff, res, v_oct};
    float out[3];
    m.process(ins, out);
    return out[port];
  };
}
}  // namespace

TEST_CASE("svf stays bounded across the res range with a hot input") {
  for (int step = 0; step <= 10; ++step) {
    const float res = static_cast<float>(step) / 10.0f;
    CAPTURE(res);
    for (float amp : {0.8f, 4.0f}) {
      auto m = make_svf();
      float peak = 0.0f;
      for (int i = 0; i < 48000; ++i) {
        const float x = amp * std::sin(static_cast<float>(2.0 * std::numbers::pi * 1000.0 * i / 48000.0));
        const float ins[4] = {x, 1000.0f, res};
        float out[3];
        m->process(ins, out);
        for (float v : out) peak = std::max(peak, std::abs(v));
      }
      CAPTURE(amp);
      CHECK(peak < 2.0f + 1.25f * amp);  // was 40 at res 1 for a 0.8 sine
    }
  }
}

TEST_CASE("svf self-oscillates at the cutoff at res 1") {
  struct Case { float cutoff, v_oct; };
  for (Case c : {Case{55.0f, 0.0f}, Case{440.0f, 0.0f}, Case{3520.0f, 0.0f}, Case{110.0f, 2.0f}}) {
    const double target = c.cutoff * std::exp2(c.v_oct);
    CAPTURE(target);
    for (int port = 0; port < 3; ++port) {
      auto m = make_svf();
      const auto tone = filter_probe::ring(svf_step(*m, port, c.cutoff, 1.0f, c.v_oct), 48000.0);
      CAPTURE(port);
      CHECK(std::abs(filter_probe::cents(tone.hz, target)) < 3.0);
      CHECK(tone.peak == doctest::Approx(1.0).epsilon(0.1));
    }
  }
}

TEST_CASE("svf rings out below the self-oscillation edge") {
  auto m = make_svf();
  const auto tone = filter_probe::ring(svf_step(*m, 0, 440.0f, 0.95f), 48000.0);
  CHECK(tone.peak < 1e-3);
}

TEST_CASE("svf res 0 is unchanged: linear, Q 0.5") {
  // -6 dB at the cutoff and exact linearity: doubling the input doubles the output.
  auto a = make_svf();
  auto b = make_svf();
  CHECK(filter_probe::sine_gain(svf_step(*a, 0, 1000.0f, 0.0f), 48000.0, 1000.0, 0.5f) ==
        doctest::Approx(0.5).epsilon(0.01));
  a = make_svf();
  float ia[4] = {0.0f, 700.0f, 0.0f}, ib[4] = {0.0f, 700.0f, 0.0f}, oa[3], ob[3];
  for (int i = 0; i < 4800; ++i) {
    ia[0] = 3.0f * std::sin(0.05f * static_cast<float>(i));
    ib[0] = 2.0f * ia[0];
    a->process(ia, oa);
    b->process(ib, ob);
    REQUIRE(ob[1] == doctest::Approx(2.0f * oa[1]).epsilon(1e-4));
  }
}

TEST_CASE("svf stays bounded under fast cutoff modulation at full res") {
  auto m = make_svf();
  float ins[4] = {0.0f, 0.0f, 1.0f};
  float out[3];
  for (int i = 0; i < 96000; ++i) {
    ins[0] = std::fmod(static_cast<float>(i) * 220.0f / 48000.0f, 1.0f) * 2.0f - 1.0f;
    ins[1] = 4000.0f + 3900.0f * std::sin(static_cast<float>(2.0 * std::numbers::pi * 1500.0 * i / 48000.0));
    m->process(ins, out);
    for (float v : out) REQUIRE(std::abs(v) < 6.0f);
  }
}
