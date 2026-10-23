#include <doctest/doctest.h>
#include "dsp/modules.h"

using namespace ms::dsp;

namespace {
float run_op(const char* op, float a, float b) {
  const ModuleInfo* info = find_module("math");
  REQUIRE(info != nullptr);
  auto m = info->create();
  Config cfg;
  ConfigValue v; v.str = op;
  cfg.push_back({"op", v});
  m->init(48000.0, cfg);
  const float ins[2] = {a, b};
  float out[1];
  m->process(ins, out);
  return out[0];
}
}  // namespace

TEST_CASE("math ops compute per-sample results") {
  CHECK(run_op("add", 0.25f, 0.5f) == doctest::Approx(0.75f));
  CHECK(run_op("sub", 0.25f, 0.5f) == doctest::Approx(-0.25f));
  CHECK(run_op("mult", 0.5f, -0.5f) == doctest::Approx(-0.25f));  // ring mod
  CHECK(run_op("min", 0.3f, -0.2f) == doctest::Approx(-0.2f));
  CHECK(run_op("max", 0.3f, -0.2f) == doctest::Approx(0.3f));
  CHECK(run_op("abs", -0.7f, 0.0f) == doctest::Approx(0.7f));  // full-wave rectifier
  CHECK(run_op("exp2", -2.0f, 0.0f) == doctest::Approx(0.25f));
  CHECK(run_op("exp2", 0.0f, 0.0f) == doctest::Approx(1.0f));
}

TEST_CASE("math clip clamps to plus-minus b") {
  CHECK(run_op("clip", 0.9f, 0.5f) == doctest::Approx(0.5f));
  CHECK(run_op("clip", -0.9f, 0.5f) == doctest::Approx(-0.5f));
  CHECK(run_op("clip", 0.3f, 0.5f) == doctest::Approx(0.3f));
}

TEST_CASE("math wrap folds into the plus-minus b range") {
  CHECK(run_op("wrap", 0.6f, 0.5f) == doctest::Approx(-0.4f));
  CHECK(run_op("wrap", -0.6f, 0.5f) == doctest::Approx(0.4f));
  CHECK(run_op("wrap", 0.2f, 0.5f) == doctest::Approx(0.2f));
  CHECK(run_op("wrap", 0.2f, 0.0f) == doctest::Approx(0.0f));  // degenerate range
}

TEST_CASE("math b defaults to zero so cables supply their actual value") {
  const ModuleInfo* info = find_module("math");
  REQUIRE(info->input_index("b") == 1);
  CHECK(info->inputs[1].def == doctest::Approx(0.0f));
}
