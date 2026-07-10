#include <doctest/doctest.h>
#include "dsp/modules.h"

using namespace ms::dsp;

// Run one instance for a while, copy state into a fresh instance, then verify
// both produce identical output from the next sample on: phase continuity.
static void check_continuity(const char* type, const float* ins, size_t warmup) {
  const ModuleInfo* info = find_module(type);
  REQUIRE(info != nullptr);
  auto a = info->create();
  auto b = info->create();
  a->init(48000.0, {});
  b->init(48000.0, {});
  float out_a[2] = {0, 0}, out_b[2] = {0, 0};
  for (size_t i = 0; i < warmup; ++i) a->process(ins, out_a);
  b->copy_state_from(*a);
  for (int i = 0; i < 8; ++i) {
    a->process(ins, out_a);
    b->process(ins, out_b);
    CHECK(out_a[0] == doctest::Approx(out_b[0]));
  }
}

TEST_CASE("vco migrates phase and sync edge state") {
  // vco inputs: freq, v_oct, fm, pw, pwm, sync
  const float ins[6] = {440.0f, 0.0f, 0.0f, 0.5f, 0.0f, 0.0f};
  check_continuity("vco", ins, 1000);
}

TEST_CASE("lfo migrates phase and reset edge state") {
  // lfo inputs: freq, reset, phase
  const float ins[3] = {2.0f, 0.0f, 0.0f};
  check_continuity("lfo", ins, 777);
}

TEST_CASE("copy_state_from on stateless modules is a safe no-op") {
  const ModuleInfo* info = find_module("vca");
  auto a = info->create();
  auto b = info->create();
  a->init(48000.0, {});
  b->init(48000.0, {});
  b->copy_state_from(*a);  // must not crash; vca has no runtime state
  float ins[3] = {0.5f, 0.0f, 1.0f}, out[1];
  b->process(ins, out);
  CHECK(out[0] == doctest::Approx(0.5f));
}
