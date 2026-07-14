#include <doctest/doctest.h>
#include <cmath>
#include <numbers>
#include <algorithm>
#include "dsp/modules.h"

using namespace ms::dsp;

namespace {
std::unique_ptr<Module> make_dcblock(double rate = 48000.0) {
  const ModuleInfo* info = find_module("dcblock");
  REQUIRE(info != nullptr);
  auto m = info->create();
  Config cfg;
  m->init(rate, cfg);
  return m;
}
}  // namespace

TEST_CASE("dcblock: metadata and default parameters") {
  const ModuleInfo* info = find_module("dcblock");
  REQUIRE(info != nullptr);
  CHECK(std::string(info->type) == "dcblock");
  
  REQUIRE(info->inputs.size() == 3);
  CHECK(std::string(info->inputs[0].name) == "in_l");
  CHECK(info->inputs[0].def == 0.0f);
  CHECK(std::string(info->inputs[1].name) == "in_r");
  CHECK(info->inputs[1].def == 0.0f);
  CHECK(std::string(info->inputs[2].name) == "cutoff");
  CHECK(info->inputs[2].def == 10.0f);
  
  REQUIRE(info->outputs.size() == 2);
  CHECK(std::string(info->outputs[0].name) == "out_l");
  CHECK(std::string(info->outputs[1].name) == "out_r");
}

TEST_CASE("dcblock: constant-DC decay below -80 dB") {
  auto m = make_dcblock(48000.0);
  float ins[3] = {1.0f, 1.0f, 10.0f};
  float outs[2] = {0.0f, 0.0f};
  
  // Feed DC input of 1.0f. With a 10 Hz cutoff, it should decay below -80 dB (0.0001) within ~0.2s (9600 samples).
  for (int i = 0; i < 12000; ++i) {
    m->process(ins, outs);
  }
  
  CHECK(std::abs(outs[0]) < 0.0001f);
  CHECK(std::abs(outs[1]) < 0.0001f);
}

TEST_CASE("dcblock: 1 kHz pass-through amplitude") {
  auto m = make_dcblock(48000.0);
  float ins[3] = {0.0f, 0.0f, 10.0f};
  float outs[2] = {0.0f, 0.0f};
  
  double phase = 0.0;
  double phase_step = 2.0 * std::numbers::pi * 1000.0 / 48000.0; // 1 kHz
  float max_out_l = 0.0f;
  float max_out_r = 0.0f;
  
  // Run for 4800 samples (100 ms) to let smoothing settle
  for (int i = 0; i < 4800; ++i) {
    ins[0] = static_cast<float>(std::sin(phase));
    ins[1] = static_cast<float>(std::cos(phase));
    phase += phase_step;
    m->process(ins, outs);
    if (i >= 4000) {
      max_out_l = std::max(max_out_l, std::abs(outs[0]));
      max_out_r = std::max(max_out_r, std::abs(outs[1]));
    }
  }
  
  // Pass-through gain should be very close to 1.0
  CHECK(max_out_l > 0.99f);
  CHECK(max_out_l <= 1.01f);
  CHECK(max_out_r > 0.99f);
  CHECK(max_out_r <= 1.01f);
}

TEST_CASE("dcblock: stereo independence") {
  {
    auto m = make_dcblock(48000.0);
    float ins[3] = {1.0f, 0.0f, 10.0f};
    float outs[2] = {0.0f, 0.0f};
    for (int i = 0; i < 100; ++i) {
      m->process(ins, outs);
      CHECK(outs[1] == 0.0f);
    }
  }
  {
    auto m = make_dcblock(48000.0);
    float ins[3] = {0.0f, 1.0f, 10.0f};
    float outs[2] = {0.0f, 0.0f};
    for (int i = 0; i < 100; ++i) {
      m->process(ins, outs);
      CHECK(outs[0] == 0.0f);
    }
  }
}

TEST_CASE("dcblock: cutoff smoothing") {
  auto m = make_dcblock(48000.0);
  float ins[3] = {1.0f, 1.0f, 10.0f};
  float outs[2] = {0.0f, 0.0f};
  
  for (int i = 0; i < 500; ++i) {
    m->process(ins, outs);
  }
  
  // Abruptly change cutoff to 40 Hz, but keep signal input constant at 1.0f
  ins[2] = 40.0f;
  
  float prev_out = outs[0];
  float max_diff = 0.0f;
  for (int i = 0; i < 100; ++i) {
    m->process(ins, outs);
    float diff = std::abs(outs[0] - prev_out);
    max_diff = std::max(max_diff, diff);
    prev_out = outs[0];
  }
  // Change should be very gradual and smooth due to 20ms one-pole smoothing
  CHECK(max_diff < 0.01f);
}


TEST_CASE("dcblock: hostile finite inputs/rates and silence") {
  for (double rate : {8000.0, 44100.0, 48000.0, 96000.0}) {
    auto m = make_dcblock(rate);
    
    // Silence
    float ins[3] = {0.0f, 0.0f, 10.0f};
    float outs[2] = {9.9f, 9.9f};
    m->process(ins, outs);
    CHECK(outs[0] == 0.0f);
    CHECK(outs[1] == 0.0f);
    
    // Hostile values (NaN, Inf, very large)
    float hostile_ins[3] = {NAN, INFINITY, 100.0f};
    m->process(hostile_ins, outs);
    CHECK(std::isfinite(outs[0]));
    CHECK(std::isfinite(outs[1]));
    
    float large_ins[3] = {1e10f, -1e10f, -100.0f};
    m->process(large_ins, outs);
    CHECK(std::isfinite(outs[0]));
    CHECK(std::isfinite(outs[1]));
  }
}
