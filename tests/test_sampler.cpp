#include <doctest/doctest.h>
#include <algorithm>
#include <cmath>
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
  float got[300], eos[300];
  for (size_t i = 0; i < 300; ++i) {
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
  CHECK(eos[4] == doctest::Approx(0.0f));
  // Stopped: the end-of-file step slews to silence over 5 ms (240 samples).
  CHECK(got[4] == doctest::Approx(0.4f));
  for (size_t i = 5; i < 300; ++i) CHECK(got[i] <= got[i - 1]);
  CHECK(got[4 + 240] == doctest::Approx(0.0f));
  CHECK(got[299] == 0.0f);
}

TEST_CASE("sampler: loop wraps around") {
  std::vector<float> ramp(1000);
  for (size_t i = 0; i < ramp.size(); ++i) ramp[i] = 0.1f + 0.0003f * static_cast<float>(i);
  auto path = write_fixture("ratsnest_loop.wav", ramp, 48000);
  auto m = make_sampler(path.string(), "on");
  float ins[3] = {0.0f, 0.0f, 1.0f};
  float outs[2];
  std::vector<float> got(1600);
  for (float& g : got) {
    m->process(ins, outs);
    g = outs[0];
  }
  std::filesystem::remove(path);
  for (size_t i = 0; i < 1000; ++i) CHECK(got[i] == doctest::Approx(ramp[i]));
  // Wrapped back to the start: the seam's ~0.3 step down is slewed out over
  // 5 ms (240 samples) instead of cut, then the buffer plays exactly again.
  float max_step = 0.0f;
  for (size_t i = 1000; i < 1300; ++i) max_step = std::max(max_step, std::abs(got[i] - got[i - 1]));
  CHECK(max_step < 0.3f / 240.0f + 0.0004f);
  for (size_t i = 1250; i < 1600; ++i) CHECK(got[i] == doctest::Approx(ramp[i - 1000]));
}

TEST_CASE("sampler: +1 v/oct pitch doubles playback rate") {
  auto path = write_fixture("ratsnest_pitch.wav", {0.1f, 0.2f, 0.3f, 0.4f}, 48000);
  auto m = make_sampler(path.string(), "off");
  float ins[3] = {0.0f, 1.0f, 1.0f};  // pitch +1 octave
  float outs[2];
  float got[300], eos[3];
  for (size_t i = 0; i < 300; ++i) {
    m->process(ins, outs);
    got[i] = outs[0];
    if (i < 3) eos[i] = outs[1];
  }
  std::filesystem::remove(path);
  CHECK(got[0] == doctest::Approx(0.1f));  // pos 0
  CHECK(got[1] == doctest::Approx(0.3f));  // pos 2 (step of 2)
  CHECK(eos[1] == doctest::Approx(1.0f));  // pos 4 -> end
  CHECK(got[299] == doctest::Approx(0.0f));  // stopped (after the slew)
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
  // Restarted at the beginning (0.1) while the old voice (at 0.3) fades.
  CHECK(outs[0] == doctest::Approx(0.1f + 0.3f));
}

TEST_CASE("sampler: missing file is silent, not a crash") {
  auto m = make_sampler("/nonexistent/nope.wav", "off");
  float ins[3] = {1.0f, 0.0f, 1.0f};
  float outs[2] = {9.0f, 9.0f};
  for (size_t i = 0; i < 10; ++i) m->process(ins, outs);
  CHECK(outs[0] == doctest::Approx(0.0f));
  CHECK(outs[1] == doctest::Approx(0.0f));
}

TEST_CASE("sampler: retrigger crossfades the old voice out instead of cutting") {
  std::vector<float> tone(48000);
  for (size_t i = 0; i < tone.size(); ++i)
    tone[i] = std::sin(2.0f * 3.14159265f * 220.0f * static_cast<float>(i) / 48000.0f);
  auto path = write_fixture("ratsnest_retrig.wav", tone, 48000);
  auto m = make_sampler(path.string(), "off");
  float ins[3] = {0.0f, 0.0f, 1.0f};
  float outs[2], prev = 0.0f, steady = 0.0f, event = 0.0f;
  for (size_t i = 0; i < 4800; ++i) {
    ins[0] = i >= 2455 ? 1.0f : 0.0f;  // retrigger near a crest
    m->process(ins, outs);
    const float step = std::abs(outs[0] - prev);
    if (i > 0 && i < 2455) {
      steady = std::max(steady, step);
      CHECK(outs[0] == doctest::Approx(tone[i]));  // untouched before the event
    }
    if (i >= 2455 && i < 2455 + 240) event = std::max(event, step);
    if (i >= 2455 + 240) CHECK(outs[0] == doctest::Approx(tone[i - 2455]));
    prev = outs[0];
  }
  std::filesystem::remove(path);
  CHECK(steady == doctest::Approx(0.0288f).epsilon(0.02));
  CHECK(event < 1.5f * steady);  // a hard cut stepped by ~1 here
}

namespace {
// Amplitude of the f-Hz component of x (Goertzel, normalized to a sine's peak).
double tone(const std::vector<float>& x, double f, double rate) {
  const double w = 2.0 * 3.14159265358979323846 * f / rate, c = 2.0 * std::cos(w);
  double s1 = 0.0, s2 = 0.0;
  for (float v : x) {
    const double s0 = v + c * s1 - s2;
    s2 = s1;
    s1 = s0;
  }
  return std::sqrt(s1 * s1 + s2 * s2 - c * s1 * s2) / (static_cast<double>(x.size()) / 2.0);
}

std::vector<float> play(const std::string& file, float pitch, size_t skip, size_t n,
                        double rate = 48000.0) {
  auto m = make_sampler(file, "on", rate);
  float ins[3] = {0.0f, pitch, 1.0f}, outs[2];
  std::vector<float> got;
  for (size_t i = 0; i < skip + n; ++i) {
    m->process(ins, outs);
    if (i >= skip) got.push_back(outs[0]);
  }
  return got;
}
}  // namespace

TEST_CASE("sampler: pitching up does not fold partials back below Nyquist") {
  // A 15 kHz partial an octave up is 30 kHz: above Nyquist, it must vanish,
  // not fold to 18 kHz. A 3 kHz partial must land at 6 kHz at full level.
  const auto sine = [](double f) {
    std::vector<float> d(48000);
    for (size_t i = 0; i < d.size(); ++i)
      d[i] = static_cast<float>(0.4 * std::sin(2.0 * 3.14159265358979323846 * f *
                                               static_cast<double>(i) / 48000.0));
    return d;
  };
  const auto high = sine(15000.0), low = sine(3000.0);
  auto hp = write_fixture("ratsnest_alias_hi.wav", high, 48000);
  auto lp = write_fixture("ratsnest_alias_lo.wav", low, 48000);
  constexpr double kFloor = 0.4 * 0.01;  // -40 dB under the partial
  CHECK(tone(play(hp.string(), 1.0f, 2000, 4800), 18000.0, 48000.0) < kFloor);
  CHECK(tone(play(lp.string(), 1.0f, 2000, 4800), 6000.0, 48000.0) ==
        doctest::Approx(0.4).epsilon(0.03));
  // Between octaves the crossfade still suppresses the fold:
  // 15 kHz x 2^1.5 = 42.4 kHz would fold to 5.6 kHz.
  CHECK(tone(play(hp.string(), 1.5f, 2000, 4800), 48000.0 - 15000.0 * std::exp2(1.5),
             48000.0) < kFloor);
  CHECK(tone(play(lp.string(), 1.5f, 2000, 4800), 3000.0 * std::exp2(1.5), 48000.0) ==
        doctest::Approx(0.4).epsilon(0.05));
  // Unpitched playback is untouched: level 0 is the file itself.
  const auto same = play(hp.string(), 0.0f, 0, 64);
  for (size_t i = 0; i < same.size(); ++i) CHECK(same[i] == doctest::Approx(high[i]));
  // A 96 kHz file at a 48 kHz engine is a step of 2 at pitch 0, decimated
  // too: its 30 kHz partial (15 kHz in samples) would fold to 18 kHz.
  auto fast = write_fixture("ratsnest_alias96.wav", high, 96000);
  CHECK(tone(play(fast.string(), 0.0f, 2000, 4800), 18000.0, 48000.0) < kFloor);
  for (const auto& f : {hp, lp, fast}) std::filesystem::remove(f);
}
