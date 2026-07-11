#include <doctest/doctest.h>
#include <cmath>
#include "dsp/modules.h"

using namespace ms::dsp;

namespace {
float run_drive(const char* mode, float in, float amount) {
  const ModuleInfo* info = find_module("drive");
  REQUIRE(info != nullptr);
  auto m = info->create();
  Config cfg;
  ConfigValue v; v.str = mode;
  cfg.push_back({"mode", v});
  m->init(48000.0, cfg);
  const float ins[2] = {in, amount};
  float out[1];
  m->process(ins, out);
  return out[0];
}
}  // namespace

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
