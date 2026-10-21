#include <doctest/doctest.h>
#include <algorithm>
#include <cmath>
#include <limits>
#include <vector>
#include <string>
#include "dsp/modules.h"

using namespace ms::dsp;

namespace {
struct AdsrRig {
  std::unique_ptr<Module> module;
  // gate, attack, decay, sustain, release
  float ins[5] = {0.0f, 0.01f, 0.02f, 0.5f, 0.04f};

  explicit AdsrRig(const char* curve = "lin") {
    const ModuleInfo* info = find_module("adsr");
    REQUIRE(info != nullptr);
    module = info->create();
    Config cfg;
    ConfigValue v; v.str = curve;
    cfg.push_back({"curve", v});
    module->init(1000.0, cfg);  // 1 kHz: attack 10 samples, decay 20, release 40
  }

  float tick(float gate) {
    ins[0] = gate;
    float out[1];
    module->process(ins, out);
    return out[0];
  }
};
}  // namespace

TEST_CASE("attack reaches one in exactly attack seconds, then decays to sustain") {
  AdsrRig env;
  float peak = 0.0f;
  int peak_at = -1;
  for (int i = 0; i < 10; ++i) {
    const float o = env.tick(1.0f);
    if (o > peak) { peak = o; peak_at = i; }
  }
  CHECK(peak == doctest::Approx(1.0f));
  CHECK(peak_at == 9);  // 10 samples of attack at 1 kHz with 0.01 s
  for (int i = 0; i < 20; ++i) (void)env.tick(1.0f);
  CHECK(env.tick(1.0f) == doctest::Approx(0.5f));  // settled on sustain
}

TEST_CASE("sustain tracks its input live while the gate is high") {
  AdsrRig env;
  for (int i = 0; i < 40; ++i) (void)env.tick(1.0f);  // well into sustain
  env.ins[3] = 0.8f;
  CHECK(env.tick(1.0f) == doctest::Approx(0.8f));
  env.ins[3] = 0.2f;
  CHECK(env.tick(1.0f) == doctest::Approx(0.2f));
}

TEST_CASE("release runs from the current level when the gate falls") {
  AdsrRig env;
  for (int i = 0; i < 40; ++i) (void)env.tick(1.0f);  // sustain 0.5
  const float first = env.tick(0.0f);
  CHECK(first < 0.5f);
  CHECK(first > 0.45f);  // one release step from 0.5, not a jump
  for (int i = 0; i < 45; ++i) (void)env.tick(0.0f);
  CHECK(env.tick(0.0f) == doctest::Approx(0.0f));  // 40-sample release done
}

TEST_CASE("a gate blip shorter than the attack releases from partway up") {
  AdsrRig env;
  float top = 0.0f;
  for (int i = 0; i < 5; ++i) top = env.tick(1.0f);  // half the attack
  CHECK(top == doctest::Approx(0.5f));
  const float after = env.tick(0.0f);  // released mid-attack
  CHECK(after < top);
  CHECK(after > 0.0f);
  float last = after;
  for (int i = 0; i < 60; ++i) last = env.tick(0.0f);
  CHECK(last == doctest::Approx(0.0f));
}

TEST_CASE("retrigger during release restarts the attack from the current level") {
  AdsrRig env;
  for (int i = 0; i < 40; ++i) (void)env.tick(1.0f);   // sustain 0.5
  for (int i = 0; i < 10; ++i) (void)env.tick(0.0f);   // partway released
  const float floor_level = env.tick(0.0f);
  CHECK(floor_level > 0.0f);
  const float retrig = env.tick(1.0f);  // rising edge again
  CHECK(retrig >= floor_level);          // no dip toward zero
  float peak = retrig;
  for (int i = 0; i < 12; ++i) peak = std::max(peak, env.tick(1.0f));
  CHECK(peak == doctest::Approx(1.0f));  // completes the new attack
}

TEST_CASE("curves shape the attack without changing its duration") {
  AdsrRig linear("lin");
  AdsrRig exponential("exp");
  AdsrRig logarithmic("log");
  AdsrRig quadratic("quad");
  float lin_mid = 0.0f, exp_mid = 0.0f, log_mid = 0.0f, quad_mid = 0.0f;
  for (int i = 0; i < 5; ++i) {
    lin_mid = linear.tick(1.0f);
    exp_mid = exponential.tick(1.0f);
    log_mid = logarithmic.tick(1.0f);
    quad_mid = quadratic.tick(1.0f);
  }
  CHECK(exp_mid > lin_mid);  // convex RC charge
  CHECK(exp_mid == doctest::Approx(1.5 * (1.0 - std::pow(3.0, -0.5))).epsilon(1e-4));
  CHECK(log_mid > lin_mid);
  CHECK(quad_mid == doctest::Approx(0.25f));
  for (int i = 5; i < 10; ++i) {
    lin_mid = linear.tick(1.0f);
    exp_mid = exponential.tick(1.0f);
    log_mid = logarithmic.tick(1.0f);
    quad_mid = quadratic.tick(1.0f);
  }
  CHECK(lin_mid == doctest::Approx(1.0f));
  CHECK(exp_mid == 1.0f);  // exact arrival, not asymptotic
  CHECK(log_mid == doctest::Approx(1.0f));
  CHECK(quad_mid == doctest::Approx(1.0f));
}

namespace {
// Decay from 1 to sustain 0 over 1 s at 1 kHz; returns the level after n
// decay samples.
std::vector<float> decay_trace(const char* curve) {
  AdsrRig env(curve);
  env.ins[1] = 0.0f;  // one-sample attack
  env.ins[2] = 1.0f;
  env.ins[3] = 0.0f;
  (void)env.tick(1.0f);  // attack lands on 1
  std::vector<float> trace;
  for (int i = 0; i < 1001; ++i) trace.push_back(env.tick(1.0f));
  return trace;
}
}  // namespace

TEST_CASE("exp decays like an RC: -30 dB at half the stage, exact arrival") {
  const auto e = decay_trace("exp");
  // (e^(-ln1000/2) - 0.001) / 0.999 = 0.0307: a natural T60 decay.
  CHECK(e[499] == doctest::Approx(0.0307).epsilon(0.01));
  for (std::size_t i = 1; i < e.size(); ++i) CHECK(e[i] <= e[i - 1]);
  CHECK(e[998] > 0.0f);  // still approaching, not yet snapped
  CHECK(e[999] == 0.0f);  // on target exactly at the stage time
  const auto q = decay_trace("quad");
  CHECK(q[499] == doctest::Approx(0.75f).epsilon(0.01));  // the old hang
  CHECK(q[999] == 0.0f);
}

TEST_CASE("exp segments survive zero, huge and non-finite times") {
  for (float t : {0.0f, 1e30f, std::numeric_limits<float>::infinity(),
                  std::numeric_limits<float>::quiet_NaN()}) {
    AdsrRig env("exp");
    env.ins[1] = env.ins[2] = env.ins[4] = t;
    for (int i = 0; i < 50; ++i) {
      const float o = env.tick(i < 25 ? 1.0f : 0.0f);
      CHECK(std::isfinite(o));
      CHECK(o >= 0.0f);
      CHECK(o <= 1.0f);
    }
  }
}

TEST_CASE("exp retrigger and release mid-stage continue from the current level") {
  AdsrRig env("exp");
  env.ins[2] = 1.0f;  // slow decay, sustain 0.5
  for (int i = 0; i < 40; ++i) (void)env.tick(1.0f);  // mid-decay
  const float before = env.tick(1.0f);
  const float released = env.tick(0.0f);
  CHECK(released < before);
  CHECK(before - released < 0.15f);  // one release step, no jump
  for (int i = 0; i < 3; ++i) (void)env.tick(0.0f);
  const float low = env.tick(0.0f);
  const float retrig = env.tick(1.0f);
  CHECK(retrig >= low);
  CHECK(retrig - low < 0.1f);  // first attack step from where it was
}

TEST_CASE("idle envelope stays at zero and output range is unipolar") {
  AdsrRig env;
  for (int i = 0; i < 100; ++i) CHECK(env.tick(0.0f) == doctest::Approx(0.0f));
  env.ins[3] = 3.0f;  // hostile sustain above range
  for (int i = 0; i < 60; ++i) {
    const float o = env.tick(1.0f);
    CHECK(o >= 0.0f);
    CHECK(o <= 1.0f + 1e-6f);  // sustain clamps to 1
  }
}
