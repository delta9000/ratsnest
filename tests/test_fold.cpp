#include <doctest/doctest.h>
#include <algorithm>
#include <array>
#include <cmath>
#include <limits>
#include <numbers>
#include <string>
#include <vector>
#include "dsp/modules.h"

using namespace ms::dsp;

namespace {
std::unique_ptr<Module> make_fold(const char* quality = nullptr) {
  const ModuleInfo* info = find_module("fold");
  REQUIRE(info != nullptr);
  auto m = info->create();
  Config cfg;
  if (quality)
    cfg.push_back({"quality", ConfigValue{quality, 0, {}, false}});
  m->init(48000.0, cfg);
  return m;
}

float run_fold(float in, float fold, float sym = 0.0f,
               const char* quality = nullptr) {
  auto m = make_fold(quality);
  const float ins[3] = {in, fold, sym};
  float out[1];
  m->process(ins, out);
  return out[0];
}

double fold_tone_magnitude(const std::vector<float>& samples, double hz) {
  double re = 0.0;
  double im = 0.0;
  for (std::size_t i = 0; i < samples.size(); ++i) {
    const double phase = 2.0 * std::numbers::pi * hz *
                         static_cast<double>(i) / 48000.0;
    re += static_cast<double>(samples[i]) * std::cos(phase);
    im -= static_cast<double>(samples[i]) * std::sin(phase);
  }
  return std::hypot(re, im);
}

std::vector<float> render_fold_tone(const char* quality, double hz,
                                    float fold, float sym) {
  auto module = make_fold(quality);
  constexpr std::size_t kWarmup = 4096;
  constexpr std::size_t kMeasure = 48000;
  std::vector<float> result(kMeasure);
  float out[1]{};
  for (std::size_t i = 0; i < kWarmup + kMeasure; ++i) {
    const float in = static_cast<float>(std::sin(
        2.0 * std::numbers::pi * hz * static_cast<double>(i) / 48000.0));
    const float ins[3] = {in, fold, sym};
    module->process(ins, out);
    if (i >= kWarmup) result[i - kWarmup] = out[0];
  }
  return result;
}
}  // namespace

TEST_CASE("fold quality metadata declares raw and clean") {
  const ModuleInfo* info = find_module("fold");
  REQUIRE(info != nullptr);
  REQUIRE(info->configs.size() == 1);
  CHECK(std::string(info->configs.back().name) == "quality");
  const char* const* enum_vals = info->configs.back().enum_vals;
  REQUIRE(enum_vals != nullptr);
  CHECK(std::string(enum_vals[0]) == "raw");
  CHECK(std::string(enum_vals[1]) == "clean");
  CHECK(enum_vals[2] == nullptr);
}

TEST_CASE("fold quality raw is bit-identical to legacy output") {
  constexpr float inputs[] = {-6.0f, -1.5f, -0.25f, 0.0f,
                              0.25f, 1.5f, 6.0f};
  constexpr float folds[] = {0.0f, 1.0f, 2.0f, 4.0f};
  constexpr float syms[] = {-0.5f, 0.0f, 0.5f};
  auto missing_module = make_fold();
  auto raw_module = make_fold("raw");

  for (float in : inputs) {
    for (float fold : folds) {
      for (float sym : syms) {
        const float x = in * std::max(fold, 0.0f) + sym;
        float t = (x + 1.0f) * 0.25f;
        t -= std::floor(t);
        const float expected = 1.0f - 4.0f * std::abs(t - 0.5f);
        const float ins[3] = {in, fold, sym};
        float missing_out[1];
        float raw_out[1];
        missing_module->process(ins, missing_out);
        raw_module->process(ins, raw_out);
        const float missing = missing_out[0];
        const float raw = raw_out[0];
        CHECK(missing == raw);
        CHECK(missing == expected);
        CHECK(raw == expected);
      }
    }
  }
}

TEST_CASE("fold is identity inside plus-minus one at unity gain") {
  CHECK(run_fold(0.0f, 1.0f) == doctest::Approx(0.0f));
  CHECK(run_fold(0.5f, 1.0f) == doctest::Approx(0.5f));
  CHECK(run_fold(-0.5f, 1.0f) == doctest::Approx(-0.5f));
  CHECK(run_fold(1.0f, 1.0f) == doctest::Approx(1.0f));
  CHECK(run_fold(-1.0f, 1.0f) == doctest::Approx(-1.0f));
}

TEST_CASE("fold reflects overshoot back into range") {
  CHECK(run_fold(1.5f, 1.0f) == doctest::Approx(0.5f));   // 1.5 folds to 0.5
  CHECK(run_fold(-1.5f, 1.0f) == doctest::Approx(-0.5f));
  CHECK(run_fold(0.75f, 2.0f) == doctest::Approx(0.5f));  // gain 2: 1.5 -> 0.5
  CHECK(run_fold(3.0f, 1.0f) == doctest::Approx(-1.0f));  // double fold
  for (float x = -6.0f; x <= 6.0f; x += 0.13f)
    CHECK(std::abs(run_fold(x, 1.0f)) <= 1.0f + 1e-6f);
}

TEST_CASE("sym offsets the input before folding") {
  CHECK(run_fold(0.8f, 1.0f, 0.5f) == doctest::Approx(0.7f));  // 1.3 folds to 0.7
  CHECK(run_fold(0.0f, 1.0f, 0.25f) == doctest::Approx(0.25f));
}

TEST_CASE("fold clean has four-sample latency and preserves settled curve") {
  auto module = make_fold("clean");
  float out[1]{};
  std::array<float, 32> impulse{};
  for (int i = 0; i < 32; ++i) {
    const float ins[3] = {i == 0 ? 0.1f : 0.0f, 1.0f, 0.0f};
    module->process(ins, out);
    impulse[static_cast<std::size_t>(i)] = out[0];
  }
  const auto peak = std::max_element(
      impulse.begin(), impulse.end(),
      [](float a, float b) { return std::abs(a) < std::abs(b); });
  CHECK(std::distance(impulse.begin(), peak) == 4);

  for (int i = 0; i < 4096; ++i) {
    const float ins[3] = {0.75f, 2.0f, 0.0f};
    module->process(ins, out);
  }
  CHECK(out[0] == doctest::Approx(0.5f).epsilon(4.0e-5));
}

TEST_CASE("fold clean suppresses the 18 kHz folded harmonic") {
  const auto raw = render_fold_tone("raw", 10000.0, 8.0f, 0.0f);
  const auto clean = render_fold_tone("clean", 10000.0, 8.0f, 0.0f);
  const double raw_alias = fold_tone_magnitude(raw, 18000.0);
  const double clean_alias = fold_tone_magnitude(clean, 18000.0);
  REQUIRE(raw_alias > 1.0);
  CHECK(clean_alias < raw_alias * 0.0316227766);  // >= 30 dB
}

TEST_CASE("fold clean hostile input cannot poison filter state") {
  auto module = make_fold("clean");
  float out[1]{};
  const float hostile[][3] = {
      {std::numeric_limits<float>::quiet_NaN(), 1.0f, 0.0f},
      {std::numeric_limits<float>::infinity(), 1.0f, 0.0f},
      {0.25f, std::numeric_limits<float>::infinity(), 0.0f},
      {0.25f, 1.0f, std::numeric_limits<float>::infinity()},
      {std::numeric_limits<float>::max(), 1.0f, 0.0f},
  };
  for (const auto& ins : hostile) {
    module->process(ins, out);
    CHECK(std::isfinite(out[0]));
  }
  const float valid[3] = {0.1f, 1.0f, 0.0f};
  for (int i = 0; i < 4096; ++i) module->process(valid, out);
  CHECK(std::isfinite(out[0]));
  CHECK(std::abs(out[0]) > 0.05f);
}

TEST_CASE("fold clean migrates matching filter and ADAA state") {
  auto source = make_fold("clean");
  auto resumed = make_fold("clean");
  float source_out[1]{};
  float resumed_out[1]{};
  for (int i = 0; i < 257; ++i) {
    const float ins[3] = {
        static_cast<float>(std::sin(0.137 * static_cast<double>(i))),
        4.0f, 0.25f};
    source->process(ins, source_out);
  }

  resumed->copy_state_from(*source);
  for (int i = 257; i < 321; ++i) {
    const float ins[3] = {
        static_cast<float>(std::sin(0.137 * static_cast<double>(i))),
        4.0f, 0.25f};
    source->process(ins, source_out);
    resumed->process(ins, resumed_out);
    CHECK(resumed_out[0] == source_out[0]);
  }
}
