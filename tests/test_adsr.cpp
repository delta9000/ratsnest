#include <doctest/doctest.h>
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
  float lin_mid = 0.0f, exp_mid = 0.0f, log_mid = 0.0f;
  for (int i = 0; i < 5; ++i) {
    lin_mid = linear.tick(1.0f);
    exp_mid = exponential.tick(1.0f);
    log_mid = logarithmic.tick(1.0f);
  }
  CHECK(exp_mid < lin_mid);
  CHECK(log_mid > lin_mid);
  for (int i = 5; i < 10; ++i) {
    lin_mid = linear.tick(1.0f);
    exp_mid = exponential.tick(1.0f);
    log_mid = logarithmic.tick(1.0f);
  }
  CHECK(lin_mid == doctest::Approx(1.0f));
  CHECK(exp_mid == doctest::Approx(1.0f));
  CHECK(log_mid == doctest::Approx(1.0f));
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
