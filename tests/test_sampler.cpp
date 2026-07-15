#include <doctest/doctest.h>
#include <filesystem>
#include <string>
#include <vector>
#include "dsp/modules.h"
#include "io/wav.h"

using namespace ms::dsp;

namespace {
std::filesystem::path write_fixture(const char* name, const std::vector<float>& mono,
                                    int rate) {
  auto path = std::filesystem::temp_directory_path() / name;
  REQUIRE(ms::io::write_wav_f32(path.string(), mono.data(), mono.size(), 1, rate));
  return path;
}

std::unique_ptr<Module> make_sampler(const std::string& file, const char* loop = "off",
                                     double rate = 48000.0) {
  const ModuleInfo* info = find_module("sampler");
  REQUIRE(info != nullptr);
  auto m = info->create();
  Config cfg;
  cfg.push_back({"file", ConfigValue{file, 0.0, {}, false}});
  cfg.push_back({"loop", ConfigValue{std::string(loop), 0.0, {}, false}});
  m->init(rate, cfg);
  return m;
}
}  // namespace

TEST_CASE("sampler: metadata") {
  const ModuleInfo* info = find_module("sampler");
  REQUIRE(info != nullptr);
  REQUIRE(info->inputs.size() == 3);
  CHECK(std::string(info->inputs[0].name) == "trig");
  CHECK(std::string(info->inputs[1].name) == "pitch");
  CHECK(std::string(info->inputs[2].name) == "level");
  REQUIRE(info->configs.size() == 2);
  CHECK(std::string(info->configs[0].name) == "file");
  CHECK(std::string(info->configs[1].name) == "loop");
  REQUIRE(info->outputs.size() == 2);
  CHECK(std::string(info->outputs[0].name) == "out");
  CHECK(std::string(info->outputs[1].name) == "eos");
}

TEST_CASE("sampler: plays a mono buffer once and emits eos at the end") {
  auto path = write_fixture("ratsnest_ramp.wav", {0.1f, 0.2f, 0.3f, 0.4f}, 48000);
  auto m = make_sampler(path.string(), "off");
  float ins[3] = {0.0f, 0.0f, 1.0f};  // trig, pitch, level
  float outs[2];
  float got[6], eos[6];
  for (size_t i = 0; i < 6; ++i) {
    m->process(ins, outs);
    got[i] = outs[0];
    eos[i] = outs[1];
  }
  std::filesystem::remove(path);
  CHECK(got[0] == doctest::Approx(0.1f));
  CHECK(got[1] == doctest::Approx(0.2f));
  CHECK(got[2] == doctest::Approx(0.3f));
  CHECK(got[3] == doctest::Approx(0.4f));
  CHECK(eos[3] == doctest::Approx(1.0f));  // eos on the sample that finishes
  CHECK(got[4] == doctest::Approx(0.0f));  // stopped
  CHECK(got[5] == doctest::Approx(0.0f));
}

TEST_CASE("sampler: loop wraps around") {
  auto path = write_fixture("ratsnest_loop.wav", {0.1f, 0.2f, 0.3f, 0.4f}, 48000);
  auto m = make_sampler(path.string(), "on");
  float ins[3] = {0.0f, 0.0f, 1.0f};
  float outs[2];
  float got[6];
  for (size_t i = 0; i < 6; ++i) {
    m->process(ins, outs);
    got[i] = outs[0];
  }
  std::filesystem::remove(path);
  CHECK(got[4] == doctest::Approx(0.1f));  // wrapped back to start
  CHECK(got[5] == doctest::Approx(0.2f));
}

TEST_CASE("sampler: +1 v/oct pitch doubles playback rate") {
  auto path = write_fixture("ratsnest_pitch.wav", {0.1f, 0.2f, 0.3f, 0.4f}, 48000);
  auto m = make_sampler(path.string(), "off");
  float ins[3] = {0.0f, 1.0f, 1.0f};  // pitch +1 octave
  float outs[2];
  float got[3], eos[3];
  for (size_t i = 0; i < 3; ++i) {
    m->process(ins, outs);
    got[i] = outs[0];
    eos[i] = outs[1];
  }
  std::filesystem::remove(path);
  CHECK(got[0] == doctest::Approx(0.1f));  // pos 0
  CHECK(got[1] == doctest::Approx(0.3f));  // pos 2 (step of 2)
  CHECK(eos[1] == doctest::Approx(1.0f));  // pos 4 -> end
  CHECK(got[2] == doctest::Approx(0.0f));  // stopped
}

TEST_CASE("sampler: level scales output") {
  auto path = write_fixture("ratsnest_level.wav", {0.8f, 0.8f}, 48000);
  auto m = make_sampler(path.string(), "off");
  float ins[3] = {0.0f, 0.0f, 0.5f};  // half level
  float outs[2];
  m->process(ins, outs);
  std::filesystem::remove(path);
  CHECK(outs[0] == doctest::Approx(0.4f));
}

TEST_CASE("sampler: trig rising edge restarts from the beginning") {
  auto path = write_fixture("ratsnest_trig.wav", {0.1f, 0.2f, 0.3f, 0.4f}, 48000);
  auto m = make_sampler(path.string(), "off");
  float ins[3] = {0.0f, 0.0f, 1.0f};
  float outs[2];
  m->process(ins, outs);  // 0.1
  m->process(ins, outs);  // 0.2
  ins[0] = 1.0f;          // trig high -> rising edge
  m->process(ins, outs);
  std::filesystem::remove(path);
  CHECK(outs[0] == doctest::Approx(0.1f));  // restarted at the beginning
}

TEST_CASE("sampler: missing file is silent, not a crash") {
  auto m = make_sampler("/nonexistent/nope.wav", "off");
  float ins[3] = {1.0f, 0.0f, 1.0f};
  float outs[2] = {9.0f, 9.0f};
  for (size_t i = 0; i < 10; ++i) m->process(ins, outs);
  CHECK(outs[0] == doctest::Approx(0.0f));
  CHECK(outs[1] == doctest::Approx(0.0f));
}
