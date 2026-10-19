#include <doctest/doctest.h>
#include <cmath>
#include <numbers>
#include <algorithm>
#include "dsp/modules.h"
#include "dsp/mastering.h"

using namespace ms::dsp;

namespace {
std::unique_ptr<Module> make_eq(double rate = 48000.0) {
  const ModuleInfo* info = find_module("eq");
  REQUIRE(info != nullptr);
  auto m = info->create();
  Config cfg;
  m->init(rate, cfg);
  return m;
}

// Compute the RMS of a signal buffer
float compute_rms(const float* buffer, size_t size) {
  double sum_sq = 0.0;
  for (size_t i = 0; i < size; ++i) {
    sum_sq += static_cast<double>(buffer[i]) * buffer[i];
  }
  return static_cast<float>(std::sqrt(sum_sq / static_cast<double>(size)));
}
}  // namespace

TEST_CASE("eq: metadata and default parameters") {
  const ModuleInfo* info = find_module("eq");
  REQUIRE(info != nullptr);
  CHECK(std::string(info->type) == "eq");
  
  REQUIRE(info->inputs.size() == 13);
  CHECK(std::string(info->inputs[0].name) == "in_l");
  CHECK(info->inputs[0].def == 0.0f);
  CHECK(std::string(info->inputs[1].name) == "in_r");
  CHECK(info->inputs[1].def == 0.0f);
  CHECK(std::string(info->inputs[2].name) == "hp_freq");
  CHECK(info->inputs[2].def == 0.0f);
  CHECK(std::string(info->inputs[3].name) == "low_freq");
  CHECK(info->inputs[3].def == 120.0f);
  CHECK(std::string(info->inputs[4].name) == "low_gain");
  CHECK(info->inputs[4].def == 0.0f);
  CHECK(std::string(info->inputs[5].name) == "mid1_freq");
  CHECK(info->inputs[5].def == 500.0f);
  CHECK(std::string(info->inputs[6].name) == "mid1_gain");
  CHECK(info->inputs[6].def == 0.0f);
  CHECK(std::string(info->inputs[7].name) == "mid1_q");
  CHECK(info->inputs[7].def == 0.707f);
  CHECK(std::string(info->inputs[8].name) == "mid2_freq");
  CHECK(info->inputs[8].def == 2500.0f);
  CHECK(std::string(info->inputs[9].name) == "mid2_gain");
  CHECK(info->inputs[9].def == 0.0f);
  CHECK(std::string(info->inputs[10].name) == "mid2_q");
  CHECK(info->inputs[10].def == 0.707f);
  CHECK(std::string(info->inputs[11].name) == "high_freq");
  CHECK(info->inputs[11].def == 10000.0f);
  CHECK(std::string(info->inputs[12].name) == "high_gain");
  CHECK(info->inputs[12].def == 0.0f);
  
  REQUIRE(info->outputs.size() == 2);
  CHECK(std::string(info->outputs[0].name) == "out_l");
  CHECK(std::string(info->outputs[1].name) == "out_r");
}

TEST_CASE("eq: default controls preserve DC and audio exactly") {
  const auto* info = find_module("eq");
  auto m = make_eq();
  float ins[13];
  for (size_t i = 0; i < 13; ++i) ins[i] = info->inputs[i].def;
  float outs[2];
  for (int i = 0; i < 1000; ++i) {
    ins[0] = static_cast<float>(i % 31) * 0.01f;
    ins[1] = -ins[0];
    m->process(ins, outs);
    CHECK(outs[0] == ins[0]);
    CHECK(outs[1] == ins[1]);
  }
}

TEST_CASE("eq: HPF attenuation and passband") {
  auto m = make_eq();
  // Set HPF to 200 Hz
  float ins[13] = {0.0f, 0.0f, 200.0f, 120.0f, 0.0f, 500.0f, 0.0f, 0.707f, 2500.0f, 0.0f, 0.707f, 10000.0f, 0.0f};
  float outs[2] = {};
  
  // 1. Feed a 10 Hz sine (far below 200 Hz cutoff) -> should be heavily attenuated
  double phase = 0.0;
  double phase_step_10 = 2.0 * std::numbers::pi * 10.0 / 48000.0;
  float max_out_10 = 0.0f;
  for (int i = 0; i < 15000; ++i) {
    ins[0] = static_cast<float>(std::sin(phase));
    phase += phase_step_10;
    m->process(ins, outs);
    if (i >= 10000) {
      max_out_10 = std::max(max_out_10, std::abs(outs[0]));
    }
  }
  // Attenuation of second-order HPF (12 dB/octave) at 10 Hz vs 200 Hz is very high (cutoff is 4.3 octaves higher)
  CHECK(max_out_10 < 0.01f); // heavily attenuated
  
  // 2. Feed a 2 kHz sine (far above 200 Hz cutoff) -> should pass through
  phase = 0.0;
  double phase_step_2k = 2.0 * std::numbers::pi * 2000.0 / 48000.0;
  float max_out_2k = 0.0f;
  for (int i = 0; i < 15000; ++i) {
    ins[0] = static_cast<float>(std::sin(phase));
    phase += phase_step_2k;
    m->process(ins, outs);
    if (i >= 10000) {
      max_out_2k = std::max(max_out_2k, std::abs(outs[0]));
    }
  }
  CHECK(max_out_2k > 0.98f); // near unity pass-through
}

TEST_CASE("eq: peaking and shelving gain accuracy within 0.15 dB") {
  auto m = make_eq();
  // Low shelf at 500 Hz, gain = +6.0 dB. Highpass is at 5.0 Hz.
  // 6 dB is a gain multiplier of 1.99526. We feed a 50 Hz sine.
  float ins[13] = {0.0f, 0.0f, 5.0f, 500.0f, 6.0f, 500.0f, 0.0f, 0.707f, 2500.0f, 0.0f, 0.707f, 10000.0f, 0.0f};
  float outs[2] = {};
  
  double phase = 0.0;
  double phase_step_50 = 2.0 * std::numbers::pi * 50.0 / 48000.0;
  float in_buf[960];
  float out_buf[960];
  
  // Run for 15000 samples to let parameter smoothing fully settle
  for (int i = 0; i < 15000; ++i) {
    float val = static_cast<float>(std::sin(phase));
    ins[0] = val;
    phase += phase_step_50;
    m->process(ins, outs);
  }
  
  // Record one cycle of panned output (960 samples at 50 Hz)
  for (int i = 0; i < 960; ++i) {
    float val = static_cast<float>(std::sin(phase));
    ins[0] = val;
    phase += phase_step_50;
    m->process(ins, outs);
    in_buf[i] = ins[0];
    out_buf[i] = outs[0];
  }
  
  float in_rms = compute_rms(in_buf, 960);
  float out_rms = compute_rms(out_buf, 960);
  float gain_db = 20.0f * std::log10(out_rms / in_rms);
  
  // Shelving gain at 50 Hz should be extremely close to 6.0 dB
  CHECK(gain_db == doctest::Approx(6.0f).epsilon(0.025f)); // within 0.15 dB
}

TEST_CASE("eq: stereo matching") {
  auto m = make_eq();
  // Identical input to Left and Right
  float ins[13] = {0.5f, 0.5f, 50.0f, 150.0f, -6.0f, 1000.0f, 12.0f, 1.0f, 3000.0f, -9.0f, 2.0f, 8000.0f, 4.0f};
  float outs[2] = {};
  
  for (int i = 0; i < 1000; ++i) {
    ins[0] = static_cast<float>(std::sin(static_cast<float>(i)));
    ins[1] = ins[0];
    m->process(ins, outs);
    CHECK(outs[0] == doctest::Approx(outs[1]));
  }
}

TEST_CASE("eq: hostile values and sample rates") {
  for (double rate : {8000.0, 44100.0, 48000.0, 96000.0}) {
    auto m = make_eq(rate);
    // Extreme frequencies (e.g. 50000 Hz at 8000 Hz SR) should be clamped safely below Nyquist
    float ins[13] = {NAN, INFINITY, 50000.0f, -100.0f, NAN, 1e6f, NAN, NAN, -50.0f, INFINITY, 100.0f, NAN, NAN};
    float outs[2] = {};
    m->process(ins, outs);
    CHECK(std::isfinite(outs[0]));
    CHECK(std::isfinite(outs[1]));
  }
}
