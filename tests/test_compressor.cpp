#include <doctest/doctest.h>
#include <cmath>
#include "dsp/modules.h"
#include "dsp/mastering.h"

using namespace ms::dsp;

namespace {
std::unique_ptr<Module> make_compressor(const char* sidechain = "internal") {
  const ModuleInfo* info = find_module("compressor");
  REQUIRE(info != nullptr);
  auto m = info->create();
  Config cfg;
  ConfigValue c; c.str = sidechain;
  cfg.push_back({"sidechain", c});
  m->init(48000.0, cfg);
  return m;
}
}  // namespace

TEST_CASE("compressor dry-wet interpolation is neutral without gain reduction") {
  for (float mix : {0.0f, 0.25f, 0.5f, 0.75f, 1.0f}) {
    auto m = make_compressor();
    float ins[11] = {0.2f, -0.3f, 0, 0, -18, 1, 0, 0.01f, 0.1f, 0, mix};
    float outs[4];
    m->process(ins, outs);
    CHECK(outs[0] == doctest::Approx(0.2f));
    CHECK(outs[1] == doctest::Approx(-0.3f));
    CHECK(outs[2] == doctest::Approx(1.0f));
  }
}

TEST_CASE("compressor: metadata and default parameters") {
  const ModuleInfo* info = find_module("compressor");
  REQUIRE(info != nullptr);
  CHECK(std::string(info->type) == "compressor");
  
  REQUIRE(info->inputs.size() == 11);
  CHECK(std::string(info->inputs[0].name) == "in_l");
  CHECK(info->inputs[0].def == 0.0f);
  CHECK(std::string(info->inputs[1].name) == "in_r");
  CHECK(info->inputs[1].def == 0.0f);
  CHECK(std::string(info->inputs[2].name) == "side_l");
  CHECK(info->inputs[2].def == 0.0f);
  CHECK(std::string(info->inputs[3].name) == "side_r");
  CHECK(info->inputs[3].def == 0.0f);
  CHECK(std::string(info->inputs[4].name) == "threshold");
  CHECK(info->inputs[4].def == -18.0f);
  CHECK(std::string(info->inputs[5].name) == "ratio");
  CHECK(info->inputs[5].def == 4.0f);
  CHECK(std::string(info->inputs[6].name) == "knee");
  CHECK(info->inputs[6].def == 6.0f);
  CHECK(std::string(info->inputs[7].name) == "attack");
  CHECK(info->inputs[7].def == 0.01f);
  CHECK(std::string(info->inputs[8].name) == "release");
  CHECK(info->inputs[8].def == 0.1f);
  CHECK(std::string(info->inputs[9].name) == "makeup");
  CHECK(info->inputs[9].def == 0.0f);
  CHECK(std::string(info->inputs[10].name) == "mix");
  CHECK(info->inputs[10].def == 1.0f);
  
  REQUIRE(info->outputs.size() == 4);
  CHECK(std::string(info->outputs[0].name) == "out_l");
  CHECK(std::string(info->outputs[1].name) == "out_r");
  CHECK(std::string(info->outputs[2].name) == "gain");
  CHECK(std::string(info->outputs[3].name) == "gr");
  
  REQUIRE(info->configs.size() == 1);
  CHECK(std::string(info->configs[0].name) == "sidechain");
}

TEST_CASE("compressor: static curve - below threshold unity") {
  auto m = make_compressor();
  // Threshold = -18 dBFS, Input = -30 dBFS (0.03162277f)
  float level = db_to_linear(-30.0f);
  float ins[11] = {level, level, 0.0f, 0.0f, -18.0f, 4.0f, 6.0f, 0.001f, 0.01f, 0.0f, 1.0f};
  float outs[4] = {};
  
  for (int i = 0; i < 2000; ++i) {
    m->process(ins, outs);
  }
  
  CHECK(std::abs(outs[3]) < 1e-4f); // 0 dB GR
  CHECK(std::abs(outs[2] - 1.0f) < 1e-4f); // 1.0 linear gain
}

TEST_CASE("compressor: static curve - hard knee ratio 4:1 above threshold") {
  auto m = make_compressor();
  // Threshold = -20 dB, Ratio = 4.0, Knee = 0.0 (hard knee)
  // Input = -12 dB (8 dB above threshold). Expected GR = 8 - 8/4 = 6 dB.
  float level = db_to_linear(-12.0f);
  float ins[11] = {level, level, 0.0f, 0.0f, -20.0f, 4.0f, 0.0f, 0.001f, 0.1f, 0.0f, 1.0f};
  float outs[4] = {};
  
  // Fast attack (1 ms), run for 1000 samples to let it settle
  for (int i = 0; i < 2000; ++i) {
    m->process(ins, outs);
  }
  
  CHECK(std::abs(outs[3] - 6.0f) < 0.01f); // 6 dB GR
  CHECK(std::abs(outs[2] - db_to_linear(-6.0f)) < 0.001f); // linear gain reduction
}

TEST_CASE("compressor: makeup gain") {
  auto m = make_compressor();
  // Threshold = -20 dB, Hard knee, Input = -12 dB, GR = 6 dB.
  // Makeup = 6.0 dB. This should exactly restore the output to -12 dB (original level).
  float level = db_to_linear(-12.0f);
  float ins[11] = {level, level, 0.0f, 0.0f, -20.0f, 4.0f, 0.0f, 0.001f, 0.1f, 6.0f, 1.0f};
  float outs[4] = {};
  
  for (int i = 0; i < 2000; ++i) {
    m->process(ins, outs);
  }
  
  CHECK(std::abs(outs[3] - 6.0f) < 0.01f); // GR remains 6 dB
  CHECK(std::abs(outs[0] - level) < 0.001f); // Output amplitude restored to input level
}

TEST_CASE("compressor: dry/wet mix") {
  auto m = make_compressor();
  // Mix = 0 (dry only)
  float level = db_to_linear(-12.0f);
  float ins[11] = {level, level, 0.0f, 0.0f, -20.0f, 4.0f, 0.0f, 0.001f, 0.1f, 0.0f, 0.0f};
  float outs[4] = {};
  
  for (int i = 0; i < 2000; ++i) {
    m->process(ins, outs);
  }
  
  // At mix = 0, equal-power dry gain is cos(0) = 1.0, wet is sin(0) = 0.0.
  // Output should exactly equal input.
  CHECK(outs[0] == doctest::Approx(level));
  CHECK(outs[1] == doctest::Approx(level));
}

TEST_CASE("compressor: external sidechain") {
  auto m = make_compressor("external");
  // Audio inputs are silent, but sidechain inputs are hot (0 dBFS = 1.0)
  // Threshold = -20 dB, Hard knee, Ratio = 2.0. Expected GR = 20 - 20/2 = 10 dB.
  float ins[11] = {0.0f, 0.0f, 1.0f, 1.0f, -20.0f, 2.0f, 0.0f, 0.001f, 0.1f, 0.0f, 1.0f};
  float outs[4] = {};
  
  for (int i = 0; i < 2000; ++i) {
    m->process(ins, outs);
  }
  
  CHECK(std::abs(outs[3] - 10.0f) < 0.01f); // 10 dB GR
}

TEST_CASE("compressor: time constants and stereo linking") {
  auto m = make_compressor();
  // Threshold = -20 dB, Hard knee, Ratio = 2.0.
  // Input: Left is 0 dBFS (1.0), Right is silent.
  // Since L & R are max-linked, both should trigger 10 dB GR.
  float ins[11] = {1.0f, 0.0f, 0.0f, 0.0f, -20.0f, 2.0f, 0.0f, 0.1f, 0.2f, 0.0f, 1.0f};
  float outs[4] = {};
  
  // 1. Check attack phase (GR rises slowly towards 10 dB)
  m->process(ins, outs);
  CHECK(outs[3] > 0.0f);
  CHECK(outs[3] < 1.0f); // only rising
  
  for (int i = 0; i < 48000; ++i) { // 1 second to settle
    m->process(ins, outs);
  }
  CHECK(std::abs(outs[3] - 10.0f) < 0.01f);
  // Equal-power wet gain is sin(pi/2) = 1.0.
  // Left output should be 1.0 * db_to_linear(-10dB) = 0.316
  // Right output should be 0.0 (since right input was 0.0) -> no image shift besides signal absence
  CHECK(std::abs(outs[0] - db_to_linear(-10.0f)) < 0.002f);
  CHECK(outs[1] == 0.0f);
  
  // 2. Check release phase
  ins[0] = 0.0f; // Silence input
  m->process(ins, outs);
  CHECK(outs[3] < 10.0f); // GR begins to drop
}

TEST_CASE("compressor: hostile values and silence") {
  auto m = make_compressor();
  float ins[11] = {NAN, INFINITY, -INFINITY, NAN, NAN, INFINITY, NAN, NAN, NAN, NAN, NAN};
  float outs[4] = {};
  
  m->process(ins, outs);
  CHECK(std::isfinite(outs[0]));
  CHECK(std::isfinite(outs[1]));
  CHECK(std::isfinite(outs[2]));
  CHECK(std::isfinite(outs[3]));
}
