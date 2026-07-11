#include <doctest/doctest.h>
#include <cmath>
#include "dsp/modules.h"

using namespace ms::dsp;

namespace {
std::unique_ptr<Module> make_mix(std::vector<double> levels = {},
                                 std::vector<double> pans = {}) {
  const ModuleInfo* info = find_module("mix");
  REQUIRE(info != nullptr);
  auto m = info->create();
  Config cfg;
  if (!levels.empty()) {
    ConfigValue v; v.arr = std::move(levels);
    cfg.push_back({"levels", v});
  }
  if (!pans.empty()) {
    ConfigValue v; v.arr = std::move(pans);
    cfg.push_back({"pans", v});
  }
  m->init(48000.0, cfg);
  return m;
}
}  // namespace

TEST_CASE("mix exposes eight signal and eight level inputs") {
  const ModuleInfo* info = find_module("mix");
  REQUIRE(info != nullptr);
  CHECK(info->input_index("in1") == 0);
  CHECK(info->input_index("in8") == 7);
  CHECK(info->input_index("lvl1") == 8);
  CHECK(info->input_index("lvl8") == 15);
  CHECK(info->output_index("out_l") == 0);
  CHECK(info->output_index("out_r") == 1);
}

TEST_CASE("mix centers a single input at equal power") {
  auto m = make_mix();
  float ins[16] = {};
  ins[0] = 1.0f;
  float out[2];
  m->process(ins, out);
  CHECK(out[0] == doctest::Approx(std::sqrt(0.5f)).epsilon(0.001));
  CHECK(out[1] == doctest::Approx(std::sqrt(0.5f)).epsilon(0.001));
}

TEST_CASE("mix pans hard left and right") {
  auto m = make_mix({1.0, 1.0}, {-1.0, 1.0});
  float ins[16] = {};
  ins[0] = 0.5f;  // hard left
  ins[1] = 0.25f; // hard right
  float out[2];
  m->process(ins, out);
  CHECK(out[0] == doctest::Approx(0.5f).epsilon(0.001));
  CHECK(out[1] == doctest::Approx(0.25f).epsilon(0.001));
}

TEST_CASE("mix levels config sets base gains and lvl inputs modulate") {
  auto m = make_mix({0.5});
  float ins[16] = {};
  ins[0] = 1.0f;
  ins[8] = 0.25f;  // lvl1 sums onto the 0.5 base
  float out[2];
  m->process(ins, out);
  CHECK(out[0] == doctest::Approx(0.75f * std::sqrt(0.5f)).epsilon(0.001));
}

TEST_CASE("mix clamps negative summed gains to zero") {
  auto m = make_mix({0.5});
  float ins[16] = {};
  ins[0] = 1.0f;
  ins[8] = -2.0f;
  float out[2];
  m->process(ins, out);
  CHECK(out[0] == doctest::Approx(0.0f));
  CHECK(out[1] == doctest::Approx(0.0f));
}
