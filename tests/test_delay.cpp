#include <doctest/doctest.h>
#include <cmath>
#include "dsp/modules.h"

using namespace ms::dsp;

namespace {
struct DelayRig {
  std::unique_ptr<Module> module;
  // in, time, pitch, feedback, mix, tone, slew
  float ins[7] = {0.0f, 0.1f, 0.0f, 0.0f, 1.0f, 8000.0f, 0.0f};

  DelayRig() {
    const ModuleInfo* info = find_module("delay");
    REQUIRE(info != nullptr);
    module = info->create();
    module->init(48000.0, {});
  }

  float tick(float in) {
    ins[0] = in;
    float out[1];
    module->process(ins, out);
    return out[0];
  }
};

// Peak magnitude and its index over [from, from+len).
struct Peak { float value; int at; };
Peak peak_in(DelayRig& d, int from, int len, int now) {
  Peak p{0.0f, -1};
  for (int i = now; i < from + len; ++i) {
    const float o = d.tick(0.0f);
    if (i >= from && std::abs(o) > std::abs(p.value)) p = {o, i};
  }
  return p;
}
}  // namespace

TEST_CASE("delay echoes an impulse after exactly time seconds") {
  DelayRig d;  // time 0.1 s, wet-only, no feedback, slew 0
  (void)d.tick(1.0f);
  const Peak p = peak_in(d, 4700, 200, 1);
  CHECK(p.at == 4800);
  CHECK(p.value == doctest::Approx(1.0f));
}

TEST_CASE("pitch input scales time exponentially, one octave per unit") {
  DelayRig d;
  d.ins[1] = 0.01f;  // 480 samples at pitch 0
  d.ins[2] = 1.0f;   // one octave up: halves to 240
  (void)d.tick(1.0f);
  const Peak p = peak_in(d, 200, 100, 1);
  CHECK(p.at == 240);
  CHECK(p.value == doctest::Approx(1.0f));
}

TEST_CASE("feedback repeats decay through the loop") {
  DelayRig d;
  d.ins[3] = 0.5f;
  d.ins[5] = 20000.0f;  // bright loop so the 2nd peak stays sharp
  (void)d.tick(1.0f);
  const Peak first = peak_in(d, 4700, 200, 1);
  const Peak second = peak_in(d, 9500, 300, 4901);
  CHECK(first.value == doctest::Approx(1.0f));
  CHECK(std::abs(second.value) > 0.3f);
  CHECK(std::abs(second.value) < 0.6f);  // ~tanh(0.5·~0.93) ≈ 0.43
}

TEST_CASE("mix is equal-power: dry at zero, minus-3dB at half") {
  DelayRig dry;
  dry.ins[4] = 0.0f;
  for (int i = 0; i < 100; ++i) {
    const float x = std::sin(0.05f * static_cast<float>(i));
    CHECK(dry.tick(x) == doctest::Approx(x));  // mix 0 == pure dry
  }
  DelayRig half;
  half.ins[4] = 0.5f;
  float last = 0.0f;
  for (int i = 0; i < 100; ++i) last = half.tick(1.0f);  // wet still silent
  CHECK(last == doctest::Approx(std::sqrt(0.5f)).epsilon(0.001));
}

TEST_CASE("slewed time changes keep the output continuous") {
  DelayRig d;
  d.ins[6] = 0.05f;  // 50 ms tape slew
  float prev = 0.0f;
  float max_jump = 0.0f;
  for (int i = 0; i < 24000; ++i) {
    if (i == 12000) d.ins[1] = 0.2f;  // yank the time knob mid-flight
    const float o = d.tick(std::sin(0.13f * static_cast<float>(i)));
    if (i > 4800) max_jump = std::max(max_jump, std::abs(o - prev));
    prev = o;
  }
  CHECK(max_jump < 0.3f);  // doppler bend, not a splice
}

TEST_CASE("tone darkens the repeats, not the first arrival's timing") {
  DelayRig darkrig;
  DelayRig brightrig;
  darkrig.ins[3] = brightrig.ins[3] = 0.7f;
  darkrig.ins[5] = 500.0f;
  brightrig.ins[5] = 12000.0f;
  (void)darkrig.tick(1.0f);
  (void)brightrig.tick(1.0f);
  const Peak dark2 = peak_in(darkrig, 9500, 300, 1);
  const Peak bright2 = peak_in(brightrig, 9500, 300, 1);
  CHECK(std::abs(dark2.value) < std::abs(bright2.value) * 0.6f);
}

TEST_CASE("feedback past unity saturates into sustained oscillation") {
  DelayRig d;
  d.ins[1] = 0.05f;
  d.ins[3] = 1.2f;
  d.ins[5] = 4000.0f;
  (void)d.tick(1.0f);
  double tail = 0.0;
  for (int i = 1; i < 144000; ++i) {  // 3 s
    const float o = d.tick(0.0f);
    REQUIRE(std::isfinite(o));
    REQUIRE(std::abs(o) < 3.0f);
    if (i >= 120000) tail += static_cast<double>(o) * o;
  }
  CHECK(std::sqrt(tail / 24000.0) > 0.005);  // still singing in the last 0.5 s
}

TEST_CASE("decayed tails flush to exact silence, no denormal residue") {
  DelayRig d;
  d.ins[3] = 0.5f;
  (void)d.tick(1.0f);
  bool silent = true;
  for (int i = 1; i < 720000; ++i) {  // 15 s of 0.1 s repeats at -7 dB each
    const float o = d.tick(0.0f);
    if (i >= 715200 && o != 0.0f) silent = false;
  }
  CHECK(silent);  // the state flush reaches exact zero, render == play
}
