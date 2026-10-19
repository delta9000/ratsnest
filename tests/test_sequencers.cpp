#include <array>
#include <cmath>
#include <doctest/doctest.h>
#include "dsp/modules.h"

using namespace ms::dsp;

namespace {
ConfigValue number(double n) {
  ConfigValue v;
  v.num = n;
  v.is_num = true;
  return v;
}
ConfigValue text(const char* s) {
  ConfigValue v;
  v.str = s;
  return v;
}
ConfigValue array(std::initializer_list<double> xs) {
  ConfigValue v;
  v.arr = xs;
  return v;
}
std::unique_ptr<Module> make(const char* type, const Config& cfg = {}) {
  const ModuleInfo* info = find_module(type);
  REQUIRE(info != nullptr);
  auto m = info->create();
  m->init(48000.0, cfg);
  return m;
}
}  // namespace

TEST_CASE("sequencer utility modules register") {
  for (const char* type : {"bern", "logic", "switch", "turing", "stepseq", "tempo"})
    CHECK(find_module(type) != nullptr);
}

TEST_CASE("bern routes a full gate exclusively at probability extremes") {
  auto m = make("bern", {{"seed", number(9)}});
  float ins[5] = {1.0f, 1.0f};
  float out[3] = {};
  m->process(ins, out);
  CHECK(out[0] == doctest::Approx(1.0f));
  CHECK(out[1] == doctest::Approx(0.0f));
  m->process(ins, out);
  CHECK(out[0] == doctest::Approx(1.0f));  // gate width is preserved
  ins[0] = 0.0f;
  m->process(ins, out);
  ins[1] = 0.0f;
  ins[0] = 1.0f;
  m->process(ins, out);
  CHECK(out[0] == doctest::Approx(0.0f));
  CHECK(out[1] == doctest::Approx(1.0f));
}

TEST_CASE("logic implements thresholded boolean and comparator modes") {
  float ins[3] = {0.8f, 0.2f, 0.5f};
  float out[1] = {};
  auto and_m = make("logic", {{"op", text("and")}});
  and_m->process(ins, out);
  CHECK(out[0] == doctest::Approx(0.0f));
  auto xor_m = make("logic", {{"op", text("xor")}});
  xor_m->process(ins, out);
  CHECK(out[0] == doctest::Approx(1.0f));
  auto not_m = make("logic", {{"op", text("not")}});
  not_m->process(ins, out);
  CHECK(out[0] == doctest::Approx(0.0f));
  auto cmp_m = make("logic", {{"op", text("cmp")}});
  cmp_m->process(ins, out);
  CHECK(out[0] == doctest::Approx(1.0f));
}

TEST_CASE("switch selects sequential and addressed inputs") {
  float ins[12] = {10, 20, 30, 40, 50, 60, 70, 80,
                   0, 0, 0, 3};  // clock reset addr n
  float out[1] = {};
  auto seq = make("switch", {{"mode", text("seq")}});
  ins[8] = 1.0f;
  seq->process(ins, out);
  CHECK(out[0] == doctest::Approx(10.0f));
  ins[8] = 0.0f;
  seq->process(ins, out);
  ins[8] = 1.0f;
  seq->process(ins, out);
  CHECK(out[0] == doctest::Approx(20.0f));

  auto addr = make("switch", {{"mode", text("addr")}});
  ins[10] = 2.0f;
  addr->process(ins, out);
  CHECK(out[0] == doctest::Approx(30.0f));
}

TEST_CASE("turing write and clear control the shift register") {
  auto m = make("turing", {{"seed", number(17)}});
  float ins[5] = {1.0f, 1.0f, 0.0f, 8.0f, 0.0f};
  float out[10] = {};
  m->process(ins, out);
  CHECK(out[0] > 0.0f);
  CHECK(out[1] == doctest::Approx(1.0f));
  CHECK(out[2] == doctest::Approx(1.0f));
  ins[0] = 0.0f;
  m->process(ins, out);
  ins[0] = 1.0f;
  ins[1] = 0.0f;
  ins[2] = 1.0f;
  m->process(ins, out);
  for (float value : out) CHECK(value == doctest::Approx(0.0f));
}

TEST_CASE("stepseq emits held CV trigger gate and end of cycle") {
  Config cfg = {{"steps", array({0.0, 0.25, 0.5})},
                {"gates", array({1.0, 0.0, 1.0})},
                {"dir", text("forward")}};
  auto m = make("stepseq", cfg);
  float ins[4] = {0.0f, 0.0f, 3.0f, 0.1f};
  float out[5] = {};
  auto edge = [&] {
    ins[0] = 0.0f;
    m->process(ins, out);
    ins[0] = 1.0f;
    m->process(ins, out);
  };
  edge();
  CHECK(out[0] == doctest::Approx(0.1f));
  CHECK(out[1] == doctest::Approx(1.0f));
  CHECK(out[2] == doctest::Approx(1.0f));
  CHECK(out[3] == doctest::Approx(0.0f));
  edge();
  CHECK(out[0] == doctest::Approx(0.35f));
  CHECK(out[1] == doctest::Approx(0.0f));
  CHECK(out[2] == doctest::Approx(0.0f));
  edge();
  CHECK(out[0] == doctest::Approx(0.6f));
  edge();
  CHECK(out[0] == doctest::Approx(0.1f));
  CHECK(out[3] == doctest::Approx(1.0f));
}

TEST_CASE("stepseq pingpong remains on a single step") {
  Config cfg = {{"steps", array({0.2})}, {"gates", array({1.0})},
                {"dir", text("pingpong")}};
  auto m = make("stepseq", cfg);
  float ins[4] = {0.0f, 0.0f, 1.0f, 0.1f};
  float out[5] = {};
  for (int i = 0; i < 4; ++i) {
    ins[0] = 0.0f;
    m->process(ins, out);
    ins[0] = 1.0f;
    m->process(ins, out);
    CHECK(out[0] == doctest::Approx(0.3f));
    CHECK(out[1] == doctest::Approx(1.0f));
  }
}

TEST_CASE("tempo converts beat lengths and remains finite at hostile BPM") {
  auto m = make("tempo");
  float ins[2] = {120.0f, 0.75f};
  float out[2] = {};
  m->process(ins, out);
  CHECK(out[0] == doctest::Approx(0.375f));
  CHECK(out[1] == doctest::Approx(1.0f / 0.375f));
  ins[0] = 0.0f;
  m->process(ins, out);
  CHECK(std::isfinite(out[0]));
  CHECK(std::isfinite(out[1]));
}

TEST_CASE("stepseq lens hold a step for several ticks with the gate sustained") {
  Config cfg = {{"steps", array({0.1, 0.2})},
                {"lens", array({3.0, 1.0})}};
  auto m = make("stepseq", cfg);
  float ins[4] = {0.0f, 0.0f, 0.0f, 0.0f};
  float out[5] = {};
  auto tick = [&](float& gate_low, float& gate_high, float& trig) {
    ins[0] = 1.0f;
    m->process(ins, out);
    gate_high = out[2]; trig = out[1];
    ins[0] = 0.0f;
    m->process(ins, out);
    gate_low = out[2];
  };
  float lo, hi, tr;
  tick(lo, hi, tr);  // step 0, tick 1 of 3
  CHECK(out[0] == doctest::Approx(0.1f));
  CHECK(tr == doctest::Approx(1.0f));
  CHECK(hi == doctest::Approx(1.0f));
  CHECK(lo == doctest::Approx(1.0f));  // sustained through the low half
  tick(lo, hi, tr);  // tick 2 of 3: still step 0, no retrigger
  CHECK(out[0] == doctest::Approx(0.1f));
  CHECK(tr == doctest::Approx(0.0f));
  CHECK(lo == doctest::Approx(1.0f));
  tick(lo, hi, tr);  // tick 3 of 3: last tick, gate drops so step 1 re-articulates
  CHECK(out[0] == doctest::Approx(0.1f));
  CHECK(tr == doctest::Approx(0.0f));
  CHECK(hi == doctest::Approx(1.0f));
  CHECK(lo == doctest::Approx(0.0f));
  tick(lo, hi, tr);  // step 1 (one tick): gate drops in its low half
  CHECK(out[0] == doctest::Approx(0.2f));
  CHECK(tr == doctest::Approx(1.0f));
  CHECK(lo == doctest::Approx(0.0f));
  tick(lo, hi, tr);  // wraps to step 0
  CHECK(out[0] == doctest::Approx(0.1f));
  CHECK(out[3] == doctest::Approx(0.0f));
  CHECK(tr == doctest::Approx(1.0f));
}

TEST_CASE("stepseq ties slur into a step without a retrigger or a gate gap") {
  Config cfg = {{"steps", array({0.1, 0.2, 0.3, 0.4})},
                {"gates", array({1.0, 1.0, 0.0, 1.0})},
                {"ties", array({0.0, 1.0, 1.0, 1.0})}};
  auto m = make("stepseq", cfg);
  float ins[4] = {0.0f, 0.0f, 0.0f, 0.0f};
  float out[5] = {};
  auto edge = [&] { ins[0] = 1.0f; m->process(ins, out); };
  auto low = [&] { ins[0] = 0.0f; m->process(ins, out); };
  edge();
  CHECK(out[1] == doctest::Approx(1.0f));  // step 0 articulates
  low();
  CHECK(out[2] == doctest::Approx(1.0f));  // no gap: step 1 is tied
  edge();
  CHECK(out[0] == doctest::Approx(0.2f));  // pitch moves (a slur)
  CHECK(out[1] == doctest::Approx(0.0f));  // ...without a trigger
  CHECK(out[2] == doctest::Approx(1.0f));
  low();
  CHECK(out[2] == doctest::Approx(0.0f));  // step 2 is a rest: the gate ends
  edge();
  CHECK(out[2] == doctest::Approx(0.0f));
  low();
  edge();  // step 3 is marked tied but follows a rest, so it starts a note
  CHECK(out[0] == doctest::Approx(0.4f));
  CHECK(out[1] == doctest::Approx(1.0f));
  CHECK(out[2] == doctest::Approx(1.0f));
}

TEST_CASE("stepseq vels are held per step and default to one") {
  Config cfg = {{"steps", array({0.0, 0.0, 0.0})},
                {"vels", array({0.5, 1.0})}};
  auto m = make("stepseq", cfg);
  float ins[4] = {0.0f, 0.0f, 0.0f, 0.0f};
  float out[5] = {};
  auto edge = [&] {
    ins[0] = 0.0f; m->process(ins, out);
    ins[0] = 1.0f; m->process(ins, out);
  };
  edge();
  CHECK(out[4] == doctest::Approx(0.5f));
  edge();
  CHECK(out[4] == doctest::Approx(1.0f));
  edge();
  CHECK(out[4] == doctest::Approx(1.0f));  // missing entry = 1
}

TEST_CASE("stepseq rhythm-only patterns sequence every gate and accent") {
  auto m = make("stepseq", {{"gates", array({1, 0, 1, 0})},
                            {"vels", array({1, 0, 0.7, 0})}});
  float ins[4] = {}, out[5] = {};
  for (int i = 0; i < 8; ++i) {
    ins[0] = 0; m->process(ins, out);
    ins[0] = 1; m->process(ins, out);
    CHECK(out[0] == 0.0f);
    CHECK(out[1] == (i % 2 == 0 ? 1.0f : 0.0f));
    CHECK(out[2] == out[1]);
    CHECK(out[3] == (i == 4 ? 1.0f : 0.0f));
    const float vel = i % 4 == 0 ? 1.0f : (i % 4 == 2 ? 0.7f : 0.0f);
    CHECK(out[4] == doctest::Approx(vel));
  }
}

TEST_CASE("stepseq rhythm-only canon triggers once after four clock ticks") {
  auto m = make("stepseq", {{"gates", array({0, 1, 0})},
                            {"lens", array({4, 1, 999})}});
  float ins[4] = {}, out[5] = {};
  for (int i = 0; i < 20; ++i) {
    ins[0] = 0; m->process(ins, out);
    ins[0] = 1; m->process(ins, out);
    CHECK(out[1] == (i == 4 ? 1.0f : 0.0f));
  }
}

TEST_CASE("stepseq omitted pitches follow the longest companion array") {
  for (const char* key : {"gates", "lens", "ties", "vels"}) {
    CAPTURE(key);
    auto m = make("stepseq", {{key, array({1, 1, 1})}});
    float ins[4] = {}, out[5] = {};
    for (int i = 0; i < 4; ++i) {
      ins[0] = 0; m->process(ins, out);
      ins[0] = 1; m->process(ins, out);
      CHECK(out[0] == 0.0f);
      CHECK(out[3] == (i == 3 ? 1.0f : 0.0f));
    }
  }
  auto m = make("stepseq", {{"steps", array({0.25})},
                            {"gates", array({1, 0, 0})}});
  float ins[4] = {}, out[5] = {};
  for (int i = 0; i < 3; ++i) {
    ins[0] = 0; m->process(ins, out);
    ins[0] = 1; m->process(ins, out);
    CHECK(out[0] == 0.25f);
    CHECK(out[1] == 1.0f);
    CHECK(out[3] == (i > 0 ? 1.0f : 0.0f));
  }
}

TEST_CASE("stepseq very large note lengths sustain without integer overflow") {
  auto m = make("stepseq", {{"steps", array({0.25, 0.5})},
                            {"lens", array({1e30, 1})}});
  float ins[4] = {}, out[5] = {};
  for (int i = 0; i < 8; ++i) {
    ins[0] = 1; m->process(ins, out);
    CHECK(out[0] == 0.25f);
    CHECK(out[1] == (i == 0 ? 1.0f : 0.0f));
    ins[0] = 0; m->process(ins, out);
    CHECK(out[2] == 1.0f);
  }
}

TEST_CASE("stepseq keeps its position in range when live length shrinks") {
  for (const char* dir : {"forward", "reverse", "pingpong", "random"}) {
    CAPTURE(dir);
    auto m = make("stepseq", {{"steps", array({0.1, 0.2, 0.3, 0.4})},
                              {"dir", text(dir)}});
    float ins[4] = {}, out[5] = {};
    for (int i = 0; i < 4; ++i) {
      ins[0] = 0; m->process(ins, out);
      ins[0] = 1; m->process(ins, out);
    }
    ins[2] = 1;
    m->process(ins, out);
    CHECK(out[0] == doctest::Approx(0.1f));
    CHECK(out[1] == 0.0f);
    for (int i = 0; i < 4; ++i) {
      ins[0] = 0; m->process(ins, out);
      ins[0] = 1; m->process(ins, out);
      CHECK(out[0] == doctest::Approx(0.1f));
    }
  }
}
