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

// The continuity tests above hold sync/reset at 0.0f throughout, so
// prev_sync_/prev_reset_ never leave their default 0.0f value in either
// instance -- they'd pass even if copy_state_from forgot to copy those
// members. These tests make the edge-memory copy load-bearing: `a` performs
// a real sync/reset edge (so its prev_* becomes 1.0f), state is copied to a
// fresh `b`, and then both are held at the triggering level. If `b`'s prev_*
// were not copied (default 0.0f), it would see a spurious fresh rising edge
// on its very next sample and re-trigger, diverging from `a`.
TEST_CASE("vco migrates prev_sync_ so a held sync input does not re-trigger") {
  const ModuleInfo* info = find_module("vco");
  REQUIRE(info != nullptr);
  auto a = info->create();
  auto b = info->create();
  a->init(48000.0, {});
  b->init(48000.0, {});

  float out_a[1] = {0}, out_b[1] = {0};
  const float ins_warm[6] = {440.0f, 0.0f, 0.0f, 0.5f, 0.0f, 0.0f};
  for (int i = 0; i < 100; ++i) a->process(ins_warm, out_a);

  // One sample with sync high: a rising edge, so `a` resets phase_ to 0 and
  // its prev_sync_ becomes 1.0.
  const float ins_sync[6] = {440.0f, 0.0f, 0.0f, 0.5f, 0.0f, 1.0f};
  a->process(ins_sync, out_a);

  b->copy_state_from(*a);

  // Hold sync high on both. `a` sees no further edge (prev_sync_ already
  // 1.0) and just advances phase normally.
  for (int i = 0; i < 8; ++i) {
    a->process(ins_sync, out_a);
    b->process(ins_sync, out_b);
    CHECK(out_a[0] == doctest::Approx(out_b[0]));
  }
}

TEST_CASE("lfo migrates prev_reset_ so a held reset input does not re-trigger") {
  const ModuleInfo* info = find_module("lfo");
  REQUIRE(info != nullptr);
  auto a = info->create();
  auto b = info->create();
  a->init(48000.0, {});
  b->init(48000.0, {});

  float out_a[1] = {0}, out_b[1] = {0};
  const float ins_warm[3] = {2.0f, 0.0f, 0.0f};
  for (int i = 0; i < 100; ++i) a->process(ins_warm, out_a);

  // One sample with reset high: a rising edge, so `a` resets phase_ to 0 and
  // its prev_reset_ becomes 1.0.
  const float ins_reset[3] = {2.0f, 1.0f, 0.0f};
  a->process(ins_reset, out_a);

  b->copy_state_from(*a);

  // Hold reset high on both. `a` sees no further edge (prev_reset_ already
  // 1.0) and just advances phase normally.
  for (int i = 0; i < 8; ++i) {
    a->process(ins_reset, out_a);
    b->process(ins_reset, out_b);
    CHECK(out_a[0] == doctest::Approx(out_b[0]));
  }
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
