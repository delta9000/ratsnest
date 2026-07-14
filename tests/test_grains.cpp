#include <doctest/doctest.h>
#include <cmath>
#include <cstddef>
#include <filesystem>
#include <limits>
#include <numbers>
#include <string>
#include <vector>
#include "dsp/modules.h"
#include "io/wav.h"

using namespace ms::dsp;

namespace {

std::unique_ptr<Module> make_grains(double rate = 48000.0, double secs = 1.0,
                                    double seed = 1.0,
                                    const char* file = nullptr) {
  const ModuleInfo* info = find_module("grains");
  REQUIRE(info != nullptr);
  auto m = info->create();
  Config cfg;
  cfg.push_back({"secs", ConfigValue{"", secs, {}, true}});
  cfg.push_back({"seed", ConfigValue{"", seed, {}, true}});
  if (file) cfg.push_back({"file", ConfigValue{std::string(file), 0.0, {}, false}});
  m->init(rate, cfg);
  return m;
}

// Test-friendly baseline, NOT the module defaults: density/jitter/shape/spread
// all 0 so nothing happens unless a test asks for it. Index map:
// {in, freeze, trig, position, scatter, size, pitch, density, jitter, shape,
//  spread, reverse}
struct Ins {
  float v[12] = {0.f, 0.f, 0.f, 0.5f, 0.f, 0.1f, 0.f, 0.f, 0.f, 0.f, 0.f, 0.f};
};

}  // namespace

TEST_CASE("grains: metadata and defaults") {
  const ModuleInfo* info = find_module("grains");
  REQUIRE(info != nullptr);
  CHECK(std::string(info->type) == "grains");

  REQUIRE(info->inputs.size() == 12);
  const char* names[12] = {"in",      "freeze", "trig",  "position",
                           "scatter", "size",   "pitch", "density",
                           "jitter",  "shape",  "spread", "reverse"};
  const float defs[12] = {0.f, 0.f, 0.f, 0.5f, 0.f, 0.1f,
                          0.f, 10.f, 0.5f, 0.5f, 0.5f, 0.f};
  for (int i = 0; i < 12; ++i) {
    CHECK(std::string(info->inputs[i].name) == names[i]);
    CHECK(info->inputs[i].def == defs[i]);
  }

  REQUIRE(info->configs.size() == 3);
  CHECK(std::string(info->configs[0].name) == "secs");
  CHECK(std::string(info->configs[1].name) == "file");
  CHECK(std::string(info->configs[2].name) == "seed");

  REQUIRE(info->outputs.size() == 3);
  CHECK(std::string(info->outputs[0].name) == "out_l");
  CHECK(std::string(info->outputs[1].name) == "out_r");
  CHECK(std::string(info->outputs[2].name) == "onset");
}

TEST_CASE("grains: silence in, exact silence out") {
  auto m = make_grains();
  Ins ins;  // density 0: no internal seeding
  float outs[3] = {1.f, 1.f, 1.f};
  for (int i = 0; i < 10000; ++i) {
    m->process(ins.v, outs);
    CHECK(outs[0] == 0.0f);
    CHECK(outs[1] == 0.0f);
    CHECK(outs[2] == 0.0f);
  }
}
