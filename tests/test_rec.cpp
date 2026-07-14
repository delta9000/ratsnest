#include <doctest/doctest.h>
#include <filesystem>
#include <string>
#include "dsp/modules.h"
#include "io/wav.h"

using namespace ms::dsp;

namespace {
std::unique_ptr<Module> make_rec(const std::string& file, double secs = 30.0,
                                 double rate = 48000.0) {
  const ModuleInfo* info = find_module("rec");
  REQUIRE(info != nullptr);
  auto m = info->create();
  Config cfg;
  cfg.push_back({"file", ConfigValue{file, 0.0, {}, false}});
  ConfigValue s;
  s.is_num = true;
  s.num = secs;
  cfg.push_back({"secs", s});
  m->init(rate, cfg);
  return m;
}
}  // namespace

TEST_CASE("rec: metadata") {
  const ModuleInfo* info = find_module("rec");
  REQUIRE(info != nullptr);
  REQUIRE(info->inputs.size() == 3);
  CHECK(std::string(info->inputs[0].name) == "in_l");
  CHECK(std::string(info->inputs[1].name) == "in_r");
  CHECK(std::string(info->inputs[2].name) == "gate");
  REQUIRE(info->configs.size() == 2);
  CHECK(std::string(info->configs[0].name) == "file");
  CHECK(std::string(info->configs[1].name) == "secs");
  CHECK(info->outputs.size() == 0);
}

TEST_CASE("rec: captures input and writes it to a wav on teardown") {
  auto path = std::filesystem::temp_directory_path() / "modsynth_rec_take.wav";
  std::filesystem::remove(path);
  {
    auto m = make_rec(path.string());
    float dummy[1];
    for (size_t i = 0; i < 5; ++i) {
      float ins[3] = {0.1f * static_cast<float>(i), -0.1f * static_cast<float>(i), 1.0f};
      m->process(ins, dummy);
    }
  }  // destructor writes the file here

  ms::io::WavData w;
  REQUIRE(ms::io::read_wav_f32(path.string(), w));
  std::filesystem::remove(path);

  CHECK(w.channels == 2);
  CHECK(w.frames == 5);
  REQUIRE(w.samples.size() == 10);
  for (size_t i = 0; i < 5; ++i) {
    CHECK(w.samples[i * 2] == doctest::Approx(0.1f * static_cast<float>(i)));
    CHECK(w.samples[i * 2 + 1] == doctest::Approx(-0.1f * static_cast<float>(i)));
  }
}

TEST_CASE("rec: gate low records nothing and writes no file") {
  auto path = std::filesystem::temp_directory_path() / "modsynth_rec_silent.wav";
  std::filesystem::remove(path);
  {
    auto m = make_rec(path.string());
    float dummy[1];
    float ins[3] = {0.5f, 0.5f, 0.0f};  // gate low
    for (size_t i = 0; i < 100; ++i) m->process(ins, dummy);
  }
  ms::io::WavData w;
  CHECK_FALSE(ms::io::read_wav_f32(path.string(), w));  // never written
  std::filesystem::remove(path);
}

TEST_CASE("rec: stops capturing at the secs cap") {
  auto path = std::filesystem::temp_directory_path() / "modsynth_rec_cap.wav";
  std::filesystem::remove(path);
  {
    // rate 100, secs 1 -> capacity 100 frames.
    auto m = make_rec(path.string(), 1.0, 100.0);
    float dummy[1];
    float ins[3] = {0.25f, 0.25f, 1.0f};
    for (size_t i = 0; i < 150; ++i) m->process(ins, dummy);  // overrun the cap
  }
  ms::io::WavData w;
  REQUIRE(ms::io::read_wav_f32(path.string(), w));
  std::filesystem::remove(path);
  CHECK(w.frames == 100);  // capped, not 150
}
