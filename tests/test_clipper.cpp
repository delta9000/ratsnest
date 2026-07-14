#include <doctest/doctest.h>
#include <cmath>
#include <numbers>
#include <algorithm>
#include "dsp/modules.h"
#include "dsp/mastering.h"

using namespace ms::dsp;

namespace {
std::unique_ptr<Module> make_clipper(const char* mode = "hard") {
  const ModuleInfo* info = find_module("clipper");
  REQUIRE(info != nullptr);
  auto m = info->create();
  Config cfg;
  ConfigValue c; c.str = mode;
  cfg.push_back({"mode", c});
  m->init(48000.0, cfg);
  return m;
}

std::unique_ptr<Module> make_drive() {
  const ModuleInfo* info = find_module("drive");
  REQUIRE(info != nullptr);
  auto m = info->create();
  Config cfg;
  ConfigValue c; c.str = "hard";
  cfg.push_back({"mode", c});
  m->init(48000.0, cfg);
  return m;
}
}  // namespace

TEST_CASE("clipper: metadata and default parameters") {
  const ModuleInfo* info = find_module("clipper");
  REQUIRE(info != nullptr);
  CHECK(std::string(info->type) == "clipper");
  
  REQUIRE(info->inputs.size() == 5);
  CHECK(std::string(info->inputs[0].name) == "in_l");
  CHECK(info->inputs[0].def == 0.0f);
  CHECK(std::string(info->inputs[1].name) == "in_r");
  CHECK(info->inputs[1].def == 0.0f);
  CHECK(std::string(info->inputs[2].name) == "threshold");
  CHECK(info->inputs[2].def == -1.0f); // dBFS
  CHECK(std::string(info->inputs[3].name) == "drive");
  CHECK(info->inputs[3].def == 0.0f); // dB
  CHECK(std::string(info->inputs[4].name) == "mix");
  CHECK(info->inputs[4].def == 1.0f);
  
  REQUIRE(info->outputs.size() == 2);
  CHECK(std::string(info->outputs[0].name) == "out_l");
  CHECK(std::string(info->outputs[1].name) == "out_r");
  
  REQUIRE(info->configs.size() == 1);
  CHECK(std::string(info->configs[0].name) == "mode");
}

TEST_CASE("clipper: impulse-measured latency and below-threshold identity") {
  auto m = make_clipper("hard");
  // threshold = 0 dBFS (1.0), drive = 0 dB, mix = 1.0
  float ins[5] = {0.0f, 0.0f, 0.0f, 0.0f, 1.0f};
  float outs[2] = {};
  
  // Send impulse on Left
  ins[0] = 1.0f;
  m->process(ins, outs);
  float out0 = outs[0];
  
  ins[0] = 0.0f;
  m->process(ins, outs);
  float out1 = outs[0];
  
  m->process(ins, outs);
  float out2 = outs[0];
  
  m->process(ins, outs);
  float out3 = outs[0];
  
  // Find which index contains the impulse peak
  float impulse_responses[4] = {out0, out1, out2, out3};
  int latency = 0;
  float max_val = 0.0f;
  for (int i = 0; i < 4; ++i) {
    if (std::abs(impulse_responses[i]) > max_val) {
      max_val = std::abs(impulse_responses[i]);
      latency = i;
    }
  }
  
  // Latency should be a small positive integer (typically 1, 2 or 3 samples depending on filter stages)
  CHECK(latency > 0);
  CHECK(latency < 4);
  
  // Below-threshold identity after latency
  // Feed a constant small value (0.1) -> output should settle to exactly 0.1 after latency
  ins[0] = 0.1f;
  for (int i = 0; i < 50; ++i) {
    m->process(ins, outs);
  }
  CHECK(outs[0] == doctest::Approx(0.1f));
  CHECK(outs[1] == doctest::Approx(0.0f));
}

TEST_CASE("clipper: hard ceiling") {
  auto m = make_clipper("hard");
  // Threshold = -6 dBFS (0.501187), Drive = 0 dB, Mix = 1.0 (wet only)
  float ins[5] = {1.0f, -1.0f, -6.0f, 0.0f, 1.0f};
  float outs[2] = {};
  
  for (int i = 0; i < 50; ++i) {
    m->process(ins, outs);
  }
  
  float threshold_linear = db_to_linear(-6.0f);
  CHECK(outs[0] == doctest::Approx(threshold_linear));
  CHECK(outs[1] == doctest::Approx(-threshold_linear));
}

TEST_CASE("clipper: odd symmetry and soft monotonicity") {
  // 1. Check symmetry in soft mode
  auto m = make_clipper("soft");
  float ins[5] = {0.8f, -0.8f, -3.0f, 0.0f, 1.0f};
  float outs[2] = {};
  
  for (int i = 0; i < 50; ++i) {
    m->process(ins, outs);
  }
  CHECK(outs[0] == doctest::Approx(-outs[1]));
  
  // 2. Check monotonicity of soft mode
  // As input drive increases, the output amplitude should rise monotonically
  float last_out = 0.0f;
  for (float drive_db = -6.0f; drive_db <= 12.0f; drive_db += 2.0f) {
    auto m_mono = make_clipper("soft");
    ins[0] = 1.0f; // Input signal
    ins[2] = -3.0f; // Threshold
    ins[3] = drive_db; // Drive
    for (int i = 0; i < 50; ++i) {
      m_mono->process(ins, outs);
    }
    CHECK(outs[0] > last_out);
    last_out = outs[0];
  }
}

TEST_CASE("clipper: spectral aliasing reduction regression") {
  // Feed a 16.1 kHz sine wave at 48 kHz SR into drive and clipper.
  // 3rd harmonic is 48.3 kHz. In 1x (drive), this aliases to 48.3 - 48.0 = 300 Hz.
  // In 4x (clipper), the alias is heavily suppressed by the decimator.
  // We filter outputs using a simple 500 Hz lowpass filter to isolate the 300 Hz alias component.
  
  auto m_drive = make_drive();
  auto m_clipper = make_clipper("hard");
  
  double phase = 0.0;
  double phase_step = 2.0 * std::numbers::pi * 16100.0 / 48000.0;
  
  // Simple OnePole lowpass filter state
  // 4-pole lowpass filter states
  float lp_drive[4] = {};
  float lp_clipper[4] = {};
  
  // OnePole coeff for 500 Hz LPF at 48 kHz
  float alpha_lp = 1.0f - std::exp(-2.0f * static_cast<float>(std::numbers::pi) * 500.0f / 48000.0f);
  
  float drive_buf[4800];
  float clipper_buf[4800];
  
  float ins_drive[2] = {0.0f, 1.0f}; // pre-gain = 1 + 9*1.0 = 10.0 (20 dB)
  float outs_drive[2] = {};
  
  float ins_clipper[5] = {0.0f, 0.0f, 0.0f, 20.0f, 1.0f}; // threshold = 0 dBFS (1.0), drive = 20.0 dB (10x)
  float outs_clipper[2] = {};
  
  // Run to let filters/oversampling settle
  for (int i = 0; i < 15000; ++i) {
    float val = static_cast<float>(std::sin(phase));
    phase += phase_step;
    
    ins_drive[0] = val;
    m_drive->process(ins_drive, outs_drive);
    
    ins_clipper[0] = val;
    m_clipper->process(ins_clipper, outs_clipper);
  }
  
  // Record and lowpass-filter
  for (int i = 0; i < 4800; ++i) {
    float val = static_cast<float>(std::sin(phase));
    phase += phase_step;
    
    ins_drive[0] = val;
    m_drive->process(ins_drive, outs_drive);
    
    // Process 4-pole LPF for drive
    float x_dr = outs_drive[0];
    lp_drive[0] += alpha_lp * (x_dr - lp_drive[0]);
    lp_drive[1] += alpha_lp * (lp_drive[0] - lp_drive[1]);
    lp_drive[2] += alpha_lp * (lp_drive[1] - lp_drive[2]);
    lp_drive[3] += alpha_lp * (lp_drive[2] - lp_drive[3]);
    drive_buf[i] = lp_drive[3];
    
    ins_clipper[0] = val;
    m_clipper->process(ins_clipper, outs_clipper);
    
    // Process 4-pole LPF for clipper
    float x_cl = outs_clipper[0];
    lp_clipper[0] += alpha_lp * (x_cl - lp_clipper[0]);
    lp_clipper[1] += alpha_lp * (lp_clipper[0] - lp_clipper[1]);
    lp_clipper[2] += alpha_lp * (lp_clipper[1] - lp_clipper[2]);
    lp_clipper[3] += alpha_lp * (lp_clipper[2] - lp_clipper[3]);
    clipper_buf[i] = lp_clipper[3];
  }
  
  float rms_drive = 0.0f;
  float rms_clipper = 0.0f;
  for (int i = 0; i < 4800; ++i) {
    rms_drive += drive_buf[i] * drive_buf[i];
    rms_clipper += clipper_buf[i] * clipper_buf[i];
  }
  rms_drive = std::sqrt(rms_drive / 4800.0f);
  rms_clipper = std::sqrt(rms_clipper / 4800.0f);
  
  // The in-band aliasing (300 Hz component) in the non-oversampled drive module
  // should be significantly larger than in the 4x oversampled clipper!
  CHECK(rms_drive > 0.01f);
  CHECK(rms_clipper < 0.01f);
  CHECK(rms_clipper < 0.05f * rms_drive); // At least 26 dB (approx 32 dB) reduction
}

TEST_CASE("clipper: silence and hostile values") {
  auto m = make_clipper();
  float ins[5] = {NAN, INFINITY, NAN, NAN, NAN};
  float outs[2] = {};
  m->process(ins, outs);
  CHECK(std::isfinite(outs[0]));
  CHECK(std::isfinite(outs[1]));
}
