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

TEST_CASE("grains: a triggered grain reproduces the buffer under its window") {
  auto m = make_grains();  // 48 kHz, secs 1 -> ring 65536, write_abs starts 65536
  Ins ins;
  float outs[3] = {0.f, 0.f, 0.f};

  // Record a known ramp: value cycles 0..0.999 with period 1000.
  for (int i = 0; i < 48000; ++i) {
    ins.v[0] = static_cast<float>(i % 1000) * 0.001f;
    m->process(ins.v, outs);
  }

  // Freeze, then trig one boxcar grain at position 0 (newest).
  ins.v[0] = 0.0f;
  ins.v[1] = 1.0f;  // freeze
  ins.v[2] = 1.0f;  // trig
  ins.v[3] = 0.0f;  // position: newest
  m->process(ins.v, outs);
  CHECK(outs[2] == 1.0f);  // onset pulse
  ins.v[2] = 0.0f;

  // Placement math (mirrors the module): dur = span = 0.1 * 48000 = 4800;
  // available = 65536 - 2*4800 - 8 = 55928; delay = 0*available + 4800 + 4;
  // write_abs at seed = 65536 + 48000 = 113536 (the frozen sample wrote
  // nothing); start = 113536 - 4804 -> ramp index 43196. At grain sample t
  // the read index is start + t, i.e. ramp index 43196 + t.
  for (int t = 1; t < 4800; ++t) {
    m->process(ins.v, outs);
    if (t == 2400) {  // boxcar plateau, exact content expected
      const float expect = static_cast<float>((43196 + 2400) % 1000) * 0.001f *
                           std::cos(std::numbers::pi_v<float> / 4.0f);
      CHECK(outs[0] == doctest::Approx(expect).epsilon(0.001));
      CHECK(outs[1] == doctest::Approx(outs[0]).epsilon(0.0001));  // spread 0
    }
  }

  // After the grain ends: exact silence again.
  for (int t = 0; t < 100; ++t) m->process(ins.v, outs);
  CHECK(outs[0] == 0.0f);
  CHECK(outs[1] == 0.0f);
}

TEST_CASE("grains: pitch +1 doubles read speed") {
  auto run = [](float pitch) {
    auto m = make_grains();
    Ins ins;
    float outs[3] = {0.f, 0.f, 0.f};
    for (int i = 0; i < 48000; ++i) {
      ins.v[0] = static_cast<float>(
          std::sin(2.0 * std::numbers::pi * 100.0 * i / 48000.0));
      m->process(ins.v, outs);
    }
    ins.v[0] = 0.0f;
    ins.v[1] = 1.0f;
    ins.v[2] = 1.0f;
    ins.v[6] = pitch;
    m->process(ins.v, outs);
    ins.v[2] = 0.0f;
    int crossings = 0;
    float prev = 0.0f;
    for (int t = 1; t < 4800; ++t) {
      m->process(ins.v, outs);
      if ((prev < 0.0f && outs[0] > 0.0f) || (prev > 0.0f && outs[0] < 0.0f))
        ++crossings;
      if (outs[0] != 0.0f) prev = outs[0];
    }
    return crossings;
  };
  // 100 Hz over a 4800-sample grain: ~20 sign changes; pitch +1 reads the
  // buffer at 2x -> ~200 Hz -> ~40.
  const int base = run(0.0f);
  const int up = run(1.0f);
  CHECK(base >= 17);
  CHECK(up >= 2 * base - 4);
  CHECK(up <= 2 * base + 4);
}

TEST_CASE("grains: reverse grain plays a ramp backward") {
  auto m = make_grains();
  Ins ins;
  float outs[3] = {0.f, 0.f, 0.f};
  for (int i = 0; i < 48000; ++i) {
    ins.v[0] = static_cast<float>(i) * 1e-5f;  // strictly increasing
    m->process(ins.v, outs);
  }
  ins.v[0] = 0.0f;
  ins.v[1] = 1.0f;
  ins.v[2] = 1.0f;
  ins.v[11] = 1.0f;  // reverse
  m->process(ins.v, outs);
  ins.v[2] = 0.0f;
  float prev = 1e9f;
  for (int t = 1; t < 4800; ++t) {
    m->process(ins.v, outs);
    if (t >= 500 && t <= 4300) {  // boxcar plateau: env == 1 exactly
      CHECK(outs[0] < prev);
      prev = outs[0];
    }
  }
}

TEST_CASE("grains: freeze keeps loud input out of the buffer") {
  auto m = make_grains();
  Ins ins;
  float outs[3] = {0.f, 0.f, 0.f};
  ins.v[0] = 1.0f;  // loud input...
  ins.v[1] = 1.0f;  // ...but frozen from the very first sample
  for (int i = 0; i < 2000; ++i) m->process(ins.v, outs);
  ins.v[2] = 1.0f;
  m->process(ins.v, outs);
  ins.v[2] = 0.0f;
  for (int t = 0; t < 4800; ++t) {
    m->process(ins.v, outs);
    CHECK(outs[0] == 0.0f);  // the buffer only ever held silence
  }
}
