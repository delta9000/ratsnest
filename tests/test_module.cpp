#include <doctest/doctest.h>
#include "dsp/modules.h"

using namespace ms::dsp;

TEST_CASE("registry finds vca and out; rejects unknown") {
  REQUIRE(find_module("vca") != nullptr);
  REQUIRE(find_module("out") != nullptr);
  CHECK(find_module("flanger") == nullptr);
}

TEST_CASE("vca metadata and slot lookup") {
  const ModuleInfo* m = find_module("vca");
  CHECK(m->input_index("in") == 0);
  CHECK(m->input_index("cv") == 1);
  CHECK(m->input_index("gain") == 2);
  CHECK(m->input_index("nope") == -1);
  CHECK(m->output_index("out") == 0);
  REQUIRE(m->config("response") != nullptr);
  CHECK(m->config("response")->enum_vals[0] == std::string("lin"));
}

TEST_CASE("vca math: gain sums with cv, clamps at zero") {
  const ModuleInfo* info = find_module("vca");
  auto m = info->create();
  m->init(48000.0, {});
  float ins[3] = {0.5f, 0.0f, 1.0f};  // in, cv, gain
  float outs[1];
  m->process(ins, outs);
  CHECK(outs[0] == doctest::Approx(0.5f));
  ins[1] = -2.0f;  // cv drives gain below zero -> silence
  m->process(ins, outs);
  CHECK(outs[0] == doctest::Approx(0.0f));
}

TEST_CASE("vca exp response squares the gain") {
  const ModuleInfo* info = find_module("vca");
  auto m = info->create();
  Config cfg;
  cfg.push_back({"response", ConfigValue{"exp", 0, {}, false}});
  m->init(48000.0, cfg);
  float ins[3] = {1.0f, 0.0f, 0.5f};
  float outs[1];
  m->process(ins, outs);
  CHECK(outs[0] == doctest::Approx(0.25f));
}

TEST_CASE("out module mixes 'in' into both sides and applies level") {
  const ModuleInfo* info = find_module("out");
  auto m = info->create();
  m->init(48000.0, {});
  float ins[4] = {0.5f, 0.25f, 0.0f, 0.5f};  // in, left, right, level
  float outs[2];
  m->process(ins, outs);
  CHECK(outs[0] == doctest::Approx(0.375f));  // (0.25 + 0.5) * 0.5
  CHECK(outs[1] == doctest::Approx(0.25f));   // (0.0  + 0.5) * 0.5
}
