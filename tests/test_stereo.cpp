#include <doctest/doctest.h>
#include <cmath>
#include <numbers>
#include "dsp/modules.h"

using namespace ms::dsp;

namespace {
std::unique_ptr<Module> make_stereo() {
  const ModuleInfo* info = find_module("stereo");
  REQUIRE(info != nullptr);
  auto m = info->create();
  Config cfg;
  m->init(48000.0, cfg);
  return m;
}
}  // namespace

TEST_CASE("stereo: metadata and default parameters") {
  const ModuleInfo* info = find_module("stereo");
  REQUIRE(info != nullptr);
  CHECK(std::string(info->type) == "stereo");
  
  REQUIRE(info->inputs.size() == 6);
  CHECK(std::string(info->inputs[0].name) == "in_l");
  CHECK(info->inputs[0].def == 0.0f);
  CHECK(std::string(info->inputs[1].name) == "in_r");
  CHECK(info->inputs[1].def == 0.0f);
  CHECK(std::string(info->inputs[2].name) == "width");
  CHECK(info->inputs[2].def == 1.0f);
  CHECK(std::string(info->inputs[3].name) == "balance");
  CHECK(info->inputs[3].def == 0.0f);
  CHECK(std::string(info->inputs[4].name) == "mono");
  CHECK(info->inputs[4].def == 0.0f);
  CHECK(std::string(info->inputs[5].name) == "swap");
  CHECK(info->inputs[5].def == 0.0f);
  
  REQUIRE(info->outputs.size() == 5);
  CHECK(std::string(info->outputs[0].name) == "out_l");
  CHECK(std::string(info->outputs[1].name) == "out_r");
  CHECK(std::string(info->outputs[2].name) == "mid");
  CHECK(std::string(info->outputs[3].name) == "side");
  CHECK(std::string(info->outputs[4].name) == "corr");
}

TEST_CASE("stereo: width identity, mono, and doubled side") {
  auto m = make_stereo();
  float ins[6] = {0.8f, -0.4f, 1.0f, 0.0f, 0.0f, 0.0f}; // width=1
  float outs[5] = {};
  
  // 1. Width = 1 (Identity, though panned center gets -3dB equal-power balance gain)
  // Run for 15000 samples to let parameter smoothing fully settle (15 time constants)
  for (int i = 0; i < 15000; ++i) {
    m->process(ins, outs);
  }
  // At balance = 0, gain_l = gain_r = cos(pi/4) = 0.70710678
  CHECK(outs[0] == doctest::Approx(0.8f * 0.70710678f));
  CHECK(outs[1] == doctest::Approx(-0.4f * 0.70710678f));
  
  // 2. Width = 0 (Mono)
  ins[2] = 0.0f;
  for (int i = 0; i < 15000; ++i) {
    m->process(ins, outs);
  }
  // Mid of input: (0.8 + -0.4) / 2 = 0.2
  // After balance pan: 0.2 * 0.70710678 = 0.1414213
  CHECK(outs[0] == doctest::Approx(0.1414213f));
  CHECK(outs[1] == doctest::Approx(0.1414213f));
  
  // 3. Width = 2 (Doubled side, zero mid)
  ins[2] = 2.0f;
  for (int i = 0; i < 15000; ++i) {
    m->process(ins, outs);
  }
  // Input Side: (0.8 - -0.4) / 2 = 0.6
  // Processed Mid = 0, Side = 2 * 0.6 = 1.2
  // Processed L = 1.2, R = -1.2
  // Balanced: L = 1.2 * 0.70710678 = 0.848528, R = -1.2 * 0.70710678 = -0.848528
  CHECK(std::abs(outs[0] - 0.848528f) < 1e-4f);
  CHECK(std::abs(outs[1] - -0.848528f) < 1e-4f);
}

TEST_CASE("stereo: equal-power balance panning") {
  auto m = make_stereo();
  float ins[6] = {1.0f, 1.0f, 1.0f, 0.0f, 0.0f, 0.0f};
  float outs[5] = {};
  
  // 1. Center (0.0) -> cos(pi/4) = 0.7071
  for (int i = 0; i < 15000; ++i) m->process(ins, outs);
  CHECK(outs[0] == doctest::Approx(0.70710678f));
  CHECK(outs[1] == doctest::Approx(0.70710678f));
  
  // 2. Hard-left (-1.0) -> Left=1.0, Right=0.0
  ins[3] = -1.0f;
  for (int i = 0; i < 15000; ++i) m->process(ins, outs);
  CHECK(std::abs(outs[0] - 1.0f) < 1e-4f);
  CHECK(std::abs(outs[1]) < 1e-4f);
  
  // 3. Hard-right (1.0) -> Left=0.0, Right=1.0
  ins[3] = 1.0f;
  for (int i = 0; i < 15000; ++i) m->process(ins, outs);
  CHECK(std::abs(outs[0]) < 1e-4f);
  CHECK(std::abs(outs[1] - 1.0f) < 1e-4f);
}

TEST_CASE("stereo: mono and swap gate semantics") {
  auto m = make_stereo();
  float ins[6] = {0.8f, -0.2f, 1.0f, 0.0f, 0.0f, 0.0f};
  float outs[5] = {};
  
  // Settle at center
  for (int i = 0; i < 15000; ++i) m->process(ins, outs);
  
  // Swap gate high (>0.5)
  ins[5] = 1.0f;
  m->process(ins, outs);
  // Unswapped Left would be 0.8 * 0.7071 = 0.56568, Right would be -0.2 * 0.7071 = -0.14142
  // Swapped: Left is -0.14142, Right is 0.56568
  CHECK(outs[0] == doctest::Approx(-0.2f * 0.70710678f));
  CHECK(outs[1] == doctest::Approx(0.8f * 0.70710678f));
  
  // Mono gate high (>0.5), swap low
  ins[5] = 0.0f;
  ins[4] = 1.0f;
  m->process(ins, outs);
  // Mono forces both to be the mid of panned outputs:
  // L_bal = 0.56568, R_bal = -0.14142
  // L_final = R_final = (0.56568 + -0.14142) / 2 = 0.21213
  CHECK(outs[0] == doctest::Approx(0.21213f));
  CHECK(outs[1] == doctest::Approx(0.21213f));
}

TEST_CASE("stereo: exact mid/side outputs") {
  auto m = make_stereo();
  float ins[6] = {0.5f, 0.3f, 1.2f, -0.4f, 0.0f, 1.0f};
  float outs[5] = {};
  
  m->process(ins, outs);
  CHECK(outs[2] == doctest::Approx((outs[0] + outs[1]) * 0.5f));
  CHECK(outs[3] == doctest::Approx((outs[0] - outs[1]) * 0.5f));
}

TEST_CASE("stereo: correlation tracking") {
  auto m = make_stereo();
  float ins[6] = {0.0f, 0.0f, 1.0f, 0.0f, 0.0f, 0.0f};
  float outs[5] = {};
  
  // 1. Identical signals (Correlation +1.0)
  for (int i = 0; i < 80000; ++i) {
    float val = std::sin(2.0f * std::numbers::pi_v<float> * 100.0f * static_cast<float>(i) / 48000.0f);
    ins[0] = val;
    ins[1] = val;
    m->process(ins, outs);
  }
  CHECK(outs[4] == doctest::Approx(1.0f).epsilon(0.01));
  
  // 2. Inverted signals (Correlation -1.0)
  for (int i = 0; i < 80000; ++i) {
    float val = std::sin(2.0f * std::numbers::pi_v<float> * 100.0f * static_cast<float>(i) / 48000.0f);
    ins[0] = val;
    ins[1] = -val;
    m->process(ins, outs);
  }
  CHECK(outs[4] == doctest::Approx(-1.0f).epsilon(0.01));
  
  // 3. Uncorrelated (Quadrature) signals (Correlation near 0.0)
  for (int i = 0; i < 80000; ++i) {
    ins[0] = std::sin(2.0f * std::numbers::pi_v<float> * 100.0f * static_cast<float>(i) / 48000.0f);
    ins[1] = std::cos(2.0f * std::numbers::pi_v<float> * 100.0f * static_cast<float>(i) / 48000.0f);
    m->process(ins, outs);
  }
  CHECK(std::abs(outs[4]) < 0.05f);
}

TEST_CASE("stereo: hostile inputs and silence") {
  auto m = make_stereo();
  float ins[6] = {NAN, INFINITY, -INFINITY, NAN, 9.9f, -1.0f};
  float outs[5] = {};
  
  m->process(ins, outs);
  CHECK(std::isfinite(outs[0]));
  CHECK(std::isfinite(outs[1]));
  CHECK(std::isfinite(outs[2]));
  CHECK(std::isfinite(outs[3]));
  CHECK(std::isfinite(outs[4]));
}
