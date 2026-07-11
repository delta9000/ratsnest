#include <doctest/doctest.h>
#include <cmath>
#include <numbers>
#include <vector>
#include "dsp/modules.h"

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
  float ins[3] = {0.0f, cutoff, 0.0f};
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
  const float ins[3] = {1.0f, 1000.0f, 0.0f};
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
  float ins[3] = {0.37f, 2400.0f, 0.0f};
  float out[3];
  for (int i = 0; i < 100; ++i) module->process(ins, out);
  CHECK(out[0] + 2.0f * out[1] + out[2] == doctest::Approx(ins[0]).epsilon(0.0001));
}

TEST_CASE("svf stays finite under extreme audio-rate modulation") {
  auto module = make_svf();
  float ins[3] = {0.0f, 0.0f, 0.0f};
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
