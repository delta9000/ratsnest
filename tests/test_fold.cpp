#include <doctest/doctest.h>
#include <algorithm>
#include <cmath>
#include "dsp/modules.h"

using namespace ms::dsp;

namespace {
float run_fold(float in, float fold, float sym = 0.0f,
               const char* quality = nullptr) {
  const ModuleInfo* info = find_module("fold");
  REQUIRE(info != nullptr);
  auto m = info->create();
  Config cfg;
  if (quality)
    cfg.push_back({"quality", ConfigValue{quality, 0, {}, false}});
  m->init(48000.0, cfg);
  const float ins[3] = {in, fold, sym};
  float out[1];
  m->process(ins, out);
  return out[0];
}
}  // namespace

TEST_CASE("fold quality metadata declares raw and clean") {
  const ModuleInfo* info = find_module("fold");
  REQUIRE(info != nullptr);
  REQUIRE(info->configs.size() == 1);
  CHECK(std::string(info->configs.back().name) == "quality");
  CHECK(std::string(info->configs.back().enum_vals[0]) == "raw");
  CHECK(std::string(info->configs.back().enum_vals[1]) == "clean");
}

TEST_CASE("fold quality raw is bit-identical to legacy output") {
  constexpr float inputs[] = {-6.0f, -1.5f, -0.25f, 0.0f,
                              0.25f, 1.5f, 6.0f};
  constexpr float folds[] = {0.0f, 1.0f, 2.0f, 4.0f};
  constexpr float syms[] = {-0.5f, 0.0f, 0.5f};

  for (float in : inputs) {
    for (float fold : folds) {
      for (float sym : syms) {
        const float x = in * std::max(fold, 0.0f) + sym;
        float t = (x + 1.0f) * 0.25f;
        t -= std::floor(t);
        const float expected = 1.0f - 4.0f * std::abs(t - 0.5f);
        const float missing = run_fold(in, fold, sym);
        const float raw = run_fold(in, fold, sym, "raw");
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
