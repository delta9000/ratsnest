#include <doctest/doctest.h>
#include "dsp/modules.h"

using namespace ms::dsp;

TEST_CASE("scope registers with one input, a label config, and no outputs") {
  const ModuleInfo* info = find_module("scope");
  REQUIRE(info != nullptr);
  CHECK(info->input_index("in") == 0);
  CHECK(info->outputs.empty());
  CHECK(info->config("label") != nullptr);
}

TEST_CASE("scope processes samples without touching outputs") {
  const ModuleInfo* info = find_module("scope");
  REQUIRE(info != nullptr);
  auto m = info->create();
  Config cfg;
  ConfigValue v; v.str = "melody";
  cfg.push_back({"label", v});
  m->init(48000.0, cfg);
  const float ins[1] = {0.7f};
  float sentinel[1] = {123.0f};
  for (int i = 0; i < 100; ++i) m->process(ins, sentinel);
  CHECK(sentinel[0] == doctest::Approx(123.0f));  // zero outputs: untouched
}
