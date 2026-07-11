#include <doctest/doctest.h>
#include <cmath>
#include "dsp/modules.h"

using namespace ms::dsp;

namespace {
float run_mix(float a, float b, float mix) {
  const ModuleInfo* info = find_module("xfade");
  REQUIRE(info != nullptr);
  auto m = info->create();
  m->init(48000.0, {});
  const float ins[3] = {a, b, mix};
  float out[1];
  m->process(ins, out);
  return out[0];
}
}  // namespace

TEST_CASE("xfade endpoints pass one input exclusively") {
  CHECK(run_mix(0.8f, -0.3f, 0.0f) == doctest::Approx(0.8f));
  CHECK(run_mix(0.8f, -0.3f, 1.0f) == doctest::Approx(-0.3f));
  CHECK(run_mix(0.8f, -0.3f, -2.0f) == doctest::Approx(0.8f));  // clamped
  CHECK(run_mix(0.8f, -0.3f, 3.0f) == doctest::Approx(-0.3f));  // clamped
}

TEST_CASE("xfade midpoint is equal-power") {
  const float g = run_mix(1.0f, 0.0f, 0.5f);
  const float h = run_mix(0.0f, 1.0f, 0.5f);
  CHECK(g == doctest::Approx(std::sqrt(0.5f)).epsilon(0.001));
  CHECK(h == doctest::Approx(std::sqrt(0.5f)).epsilon(0.001));
  CHECK(g * g + h * h == doctest::Approx(1.0f).epsilon(0.001));
}
