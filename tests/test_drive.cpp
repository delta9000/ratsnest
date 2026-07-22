#include <doctest/doctest.h>
#include <algorithm>
#include <cmath>
#include <string>
#include <string_view>
#include "dsp/modules.h"

using namespace ms::dsp;

namespace {
std::unique_ptr<Module> make_drive(const char* mode,
                                   const char* quality = nullptr) {
  const ModuleInfo* info = find_module("drive");
  REQUIRE(info != nullptr);
  auto m = info->create();
  Config cfg;
  ConfigValue v; v.str = mode;
  cfg.push_back({"mode", v});
  if (quality)
    cfg.push_back({"quality", ConfigValue{quality, 0, {}, false}});
  m->init(48000.0, cfg);
  return m;
}

float run_drive(const char* mode, float in, float amount,
                const char* quality = nullptr) {
  auto m = make_drive(mode, quality);
  const float ins[2] = {in, amount};
  float out[1];
  m->process(ins, out);
  return out[0];
}
}  // namespace

TEST_CASE("drive quality metadata declares raw and clean") {
  const ModuleInfo* info = find_module("drive");
  REQUIRE(info != nullptr);
  REQUIRE(info->configs.size() == 2);
  CHECK(std::string(info->configs[0].name) == "mode");
  CHECK(std::string(info->configs.back().name) == "quality");
  const char* const* enum_vals = info->configs.back().enum_vals;
  REQUIRE(enum_vals != nullptr);
  CHECK(std::string(enum_vals[0]) == "raw");
  CHECK(std::string(enum_vals[1]) == "clean");
  CHECK(enum_vals[2] == nullptr);
}

TEST_CASE("drive quality raw is bit-identical to legacy output") {
  constexpr const char* modes[] = {"soft", "hard"};
  constexpr float inputs[] = {-2.0f, -0.7f, -0.1f, 0.0f,
                              0.1f, 0.7f, 2.0f};
  constexpr float amounts[] = {0.0f, 0.1f, 0.5f, 1.0f};

  for (const char* mode : modes) {
    auto missing_module = make_drive(mode);
    auto raw_module = make_drive(mode, "raw");
    for (float in : inputs) {
      for (float amount : amounts) {
        const float g = 1.0f + 9.0f * std::clamp(amount, 0.0f, 1.0f);
        const float x = in * g;
        const float expected = std::string_view(mode) == "hard"
            ? std::clamp(x, -1.0f, 1.0f)
            : std::tanh(x);
        const float ins[2] = {in, amount};
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

TEST_CASE("soft drive saturates smoothly and stays bounded") {
  CHECK(run_drive("soft", 0.0f, 1.0f) == doctest::Approx(0.0f));
  // g=10 at in=1.0 makes tanh(10) round to exactly 1.0f in float32, so use
  // in=0.8 (tanh(8) still rounds down) to check the asymptote is never hit.
  CHECK(std::abs(run_drive("soft", 0.8f, 1.0f)) < 1.0f);    // tanh never reaches 1
  CHECK(run_drive("soft", 1.0f, 1.0f) > 0.99f);            // but gets close at g=10
  CHECK(run_drive("soft", -1.0f, 1.0f) == doctest::Approx(-run_drive("soft", 1.0f, 1.0f)));
  // More amount -> more output for the same small input (gain rises)
  CHECK(run_drive("soft", 0.05f, 1.0f) > run_drive("soft", 0.05f, 0.0f));
}

TEST_CASE("hard drive clips exactly at plus-minus one") {
  CHECK(run_drive("hard", 0.5f, 1.0f) == doctest::Approx(1.0f));    // 0.5*10 clipped
  CHECK(run_drive("hard", -0.5f, 1.0f) == doctest::Approx(-1.0f));
  CHECK(run_drive("hard", 0.05f, 1.0f) == doctest::Approx(0.5f));   // linear below knee
}

TEST_CASE("drive amount zero is near-unity gain") {
  CHECK(run_drive("hard", 0.3f, 0.0f) == doctest::Approx(0.3f));
  CHECK(run_drive("soft", 0.1f, 0.0f) == doctest::Approx(std::tanh(0.1f)));
}
