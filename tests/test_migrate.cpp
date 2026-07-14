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
  float out_a[16] = {}, out_b[16] = {};
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

TEST_CASE("clock migrates phase and reset edge state") {
  // clock inputs: bpm, run, reset
  const float ins[3] = {124.0f, 1.0f, 0.0f};
  check_continuity("clock", ins, 12345);
}

TEST_CASE("function generator migrates an active envelope") {
  const ModuleInfo* info = find_module("func");
  REQUIRE(info != nullptr);
  auto a = info->create();
  auto b = info->create();
  a->init(1000.0, {});
  b->init(1000.0, {});
  float ins[4] = {1.0f, 0.0f, 0.1f, 0.2f};
  float out_a[3] = {}, out_b[3] = {};
  a->process(ins, out_a);
  ins[0] = 0.0f;
  for (int i = 0; i < 35; ++i) a->process(ins, out_a);
  b->copy_state_from(*a);
  for (int i = 0; i < 16; ++i) {
    a->process(ins, out_a);
    b->process(ins, out_b);
    CHECK(out_a[0] == doctest::Approx(out_b[0]));
    CHECK(out_a[1] == doctest::Approx(out_b[1]));
    CHECK(out_a[2] == doctest::Approx(out_b[2]));
  }
}

TEST_CASE("divider migrates counter gate and edge memory") {
  const ModuleInfo* info = find_module("div");
  REQUIRE(info != nullptr);
  auto a = info->create();
  auto b = info->create();
  a->init(48000.0, {});
  b->init(48000.0, {});
  float ins[3] = {0.0f, 4.0f, 0.0f};
  float out_a[1] = {}, out_b[1] = {};
  for (int edge = 0; edge < 3; ++edge) {
    ins[0] = 0.0f;
    a->process(ins, out_a);
    ins[0] = 1.0f;
    a->process(ins, out_a);
  }
  b->copy_state_from(*a);
  // Start the post-copy loop with the input still high: `a`'s prev_in_ is
  // 1.0f at copy time, so a fresh `b` that forgot to copy it would see a
  // spurious rising edge here and diverge.
  for (int i = 0; i < 8; ++i) {
    ins[0] = i % 2 == 0 ? 1.0f : 0.0f;
    a->process(ins, out_a);
    b->process(ins, out_b);
    CHECK(out_a[0] == doctest::Approx(out_b[0]));
  }
}

TEST_CASE("euclid migrates step hit and both edge memories") {
  const ModuleInfo* info = find_module("euclid");
  REQUIRE(info != nullptr);
  auto a = info->create();
  auto b = info->create();
  a->init(48000.0, {});
  b->init(48000.0, {});
  // clock, reset, steps, hits, rot. A coincident reset+clock edge selects
  // step zero (a hit) and leaves both edge memories high at copy time.
  float ins[5] = {1.0f, 1.0f, 8.0f, 3.0f, 0.0f};
  float out_a[2] = {}, out_b[2] = {};
  a->process(ins, out_a);
  b->copy_state_from(*a);

  // Holding both inputs high exposes a dropped hit_, prev_clock_, or
  // prev_reset_ immediately. Subsequent edges expose a dropped step_.
  for (int i = 0; i < 20; ++i) {
    if (i == 1) ins[0] = ins[1] = 0.0f;
    if (i > 1) ins[0] = i % 2 == 0 ? 1.0f : 0.0f;
    a->process(ins, out_a);
    b->process(ins, out_b);
    CHECK(out_a[0] == doctest::Approx(out_b[0]));
    CHECK(out_a[1] == doctest::Approx(out_b[1]));
  }
}

TEST_CASE("bern migrates decision RNG and gate edge memory") {
  const ModuleInfo* info = find_module("bern");
  REQUIRE(info != nullptr);
  auto a = info->create();
  auto b = info->create();
  a->init(48000.0, {});
  b->init(48000.0, {});
  float ins[2] = {1.0f, 1.0f};
  float out_a[2] = {}, out_b[2] = {};
  a->process(ins, out_a);  // selected A, input left high
  b->copy_state_from(*a);
  for (int i = 0; i < 40; ++i) {
    ins[0] = i % 2 == 0 ? 1.0f : 0.0f;
    ins[1] = 0.5f;
    a->process(ins, out_a);
    b->process(ins, out_b);
    CHECK(out_a[0] == doctest::Approx(out_b[0]));
    CHECK(out_a[1] == doctest::Approx(out_b[1]));
  }
}

TEST_CASE("switch migrates sequential index and both edge memories") {
  const ModuleInfo* info = find_module("switch");
  REQUIRE(info != nullptr);
  auto a = info->create();
  auto b = info->create();
  a->init(48000.0, {});
  b->init(48000.0, {});
  float ins[12] = {1, 2, 3, 4, 5, 6, 7, 8, 1, 1, 0, 4};
  float out_a[1] = {}, out_b[1] = {};
  a->process(ins, out_a);  // reset+clock selects input one; edges stay high
  b->copy_state_from(*a);
  for (int i = 0; i < 16; ++i) {
    if (i == 1) ins[9] = 0.0f;
    ins[8] = i % 2 == 0 ? 1.0f : 0.0f;
    a->process(ins, out_a);
    b->process(ins, out_b);
    CHECK(out_a[0] == doctest::Approx(out_b[0]));
  }
}

TEST_CASE("turing migrates register RNG and clock edge memory") {
  const ModuleInfo* info = find_module("turing");
  REQUIRE(info != nullptr);
  auto a = info->create();
  auto b = info->create();
  a->init(48000.0, {});
  b->init(48000.0, {});
  float ins[5] = {1.0f, 1.0f, 0.0f, 11.0f, 0.37f};
  float out_a[10] = {}, out_b[10] = {};
  a->process(ins, out_a);
  b->copy_state_from(*a);
  ins[1] = 0.0f;
  for (int i = 0; i < 48; ++i) {
    ins[0] = i % 2 == 0 ? 1.0f : 0.0f;
    a->process(ins, out_a);
    b->process(ins, out_b);
    for (int j = 0; j < 10; ++j)
      CHECK(out_a[j] == doctest::Approx(out_b[j]));
  }
}

TEST_CASE("stepseq migrates position direction RNG gate and edge memories") {
  const ModuleInfo* info = find_module("stepseq");
  REQUIRE(info != nullptr);
  Config cfg;
  ConfigValue steps; steps.arr = {0.0, 0.2, 0.4, 0.7};
  ConfigValue gates; gates.arr = {1.0, 0.0, 1.0, 1.0};
  ConfigValue dir; dir.str = "random";
  ConfigValue seed; seed.num = 23.0; seed.is_num = true;
  cfg.push_back({"steps", steps}); cfg.push_back({"gates", gates});
  cfg.push_back({"dir", dir}); cfg.push_back({"seed", seed});
  auto a = info->create();
  auto b = info->create();
  a->init(48000.0, cfg);
  b->init(48000.0, cfg);
  float ins[4] = {1.0f, 1.0f, 4.0f, 0.1f};
  float out_a[4] = {}, out_b[4] = {};
  a->process(ins, out_a);
  b->copy_state_from(*a);
  for (int i = 0; i < 48; ++i) {
    if (i == 1) ins[1] = 0.0f;
    ins[0] = i % 2 == 0 ? 1.0f : 0.0f;
    a->process(ins, out_a);
    b->process(ins, out_b);
    for (int j = 0; j < 4; ++j)
      CHECK(out_a[j] == doctest::Approx(out_b[j]));
  }
}

// A constant DC input would let ic1eq_ (the band-pass integrator) decay to
// its default 0.0f during warmup, making its copy invisible. A square wave
// near cutoff keeps both integrators far from zero at the copy point.
TEST_CASE("svf migrates both integrator states") {
  const ModuleInfo* info = find_module("svf");
  REQUIRE(info != nullptr);
  auto a = info->create();
  auto b = info->create();
  a->init(48000.0, {});
  b->init(48000.0, {});
  float ins[3] = {0.0f, 1700.0f, 0.65f};
  float out_a[3] = {}, out_b[3] = {};
  for (int i = 0; i < 913; ++i) {
    ins[0] = (i / 16) % 2 == 0 ? 0.8f : -0.8f;
    a->process(ins, out_a);
  }
  b->copy_state_from(*a);
  for (int i = 913; i < 913 + 32; ++i) {
    ins[0] = (i / 16) % 2 == 0 ? 0.8f : -0.8f;
    a->process(ins, out_a);
    b->process(ins, out_b);
    CHECK(out_a[0] == doctest::Approx(out_b[0]));
    CHECK(out_a[1] == doctest::Approx(out_b[1]));
    CHECK(out_a[2] == doctest::Approx(out_b[2]));
  }
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

TEST_CASE("clock migrates prev_reset_ so a held reset input does not re-trigger") {
  const ModuleInfo* info = find_module("clock");
  REQUIRE(info != nullptr);
  auto a = info->create();
  auto b = info->create();
  // Low rate so the one-sample phase offset a forgotten prev_reset_ copy
  // causes would cross the gate's 0.5 boundary within the compare window.
  a->init(100.0, {});
  b->init(100.0, {});

  float out_a[2] = {0, 0}, out_b[2] = {0, 0};
  const float ins_warm[3] = {60.0f, 1.0f, 0.0f};
  for (int i = 0; i < 30; ++i) a->process(ins_warm, out_a);

  // One sample with reset high: `a` snaps phase_ to 0, prev_reset_ becomes 1.
  const float ins_reset[3] = {60.0f, 1.0f, 1.0f};
  a->process(ins_reset, out_a);

  b->copy_state_from(*a);

  // Hold reset high on both across a full half-cycle (50 samples at 60 BPM /
  // 100 Hz): a spurious re-reset in `b` would delay its falling edge by one
  // sample and mismatch at the boundary.
  for (int i = 0; i < 60; ++i) {
    a->process(ins_reset, out_a);
    b->process(ins_reset, out_b);
    CHECK(out_a[0] == doctest::Approx(out_b[0]));
  }
}

TEST_CASE("div migrates prev_reset_ so a held reset input does not re-clear") {
  const ModuleInfo* info = find_module("div");
  REQUIRE(info != nullptr);
  auto a = info->create();
  auto b = info->create();
  a->init(48000.0, {});
  b->init(48000.0, {});

  float out_a[1] = {0}, out_b[1] = {0};
  float ins[3] = {0.0f, 4.0f, 0.0f};

  // Reset edge on `a`, then keep reset held high while counting two input
  // edges so count_ is mid-cycle at the copy point.
  ins[2] = 1.0f;
  a->process(ins, out_a);
  for (int edge = 0; edge < 2; ++edge) {
    ins[0] = 0.0f;
    a->process(ins, out_a);
    ins[0] = 1.0f;
    a->process(ins, out_a);
  }

  b->copy_state_from(*a);

  // Reset stays high. If `b` forgot prev_reset_ it would see a fresh reset
  // edge, clear its counter, and disagree on the following edges.
  for (int i = 0; i < 8; ++i) {
    ins[0] = i % 2 == 0 ? 0.0f : 1.0f;
    a->process(ins, out_a);
    b->process(ins, out_b);
    CHECK(out_a[0] == doctest::Approx(out_b[0]));
  }
}

TEST_CASE("func migrates prev_trig_ so a held trigger does not re-trigger") {
  const ModuleInfo* info = find_module("func");
  REQUIRE(info != nullptr);
  auto a = info->create();
  auto b = info->create();
  a->init(1000.0, {});
  b->init(1000.0, {});

  float out_a[3] = {}, out_b[3] = {};
  const float ins[4] = {1.0f, 0.0f, 0.1f, 0.2f};  // trig held high throughout
  for (int i = 0; i < 20; ++i) a->process(ins, out_a);  // mid-rise

  b->copy_state_from(*a);

  // If `b` forgot prev_trig_ it would re-trigger, restarting the rise from
  // the copied level and diverging immediately.
  for (int i = 0; i < 16; ++i) {
    a->process(ins, out_a);
    b->process(ins, out_b);
    CHECK(out_a[0] == doctest::Approx(out_b[0]));
    CHECK(out_a[1] == doctest::Approx(out_b[1]));
    CHECK(out_a[2] == doctest::Approx(out_b[2]));
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

TEST_CASE("sh migrates held value and prev_trig_ edge memory") {
  const ModuleInfo* info = find_module("sh");
  REQUIRE(info != nullptr);
  auto a = info->create();
  auto b = info->create();
  a->init(48000.0, {});
  b->init(48000.0, {});
  float out_a[1], out_b[1];
  float ins[2] = {0.55f, 0.0f};
  a->process(ins, out_a);
  ins[1] = 1.0f;  // rising edge: a latches 0.55, prev_trig_ becomes 1.0
  a->process(ins, out_a);
  b->copy_state_from(*a);
  // Trigger stays high but the input changes: a fresh prev_trig_ (0.0)
  // would see a spurious edge and latch -0.9 instead of holding 0.55.
  ins[0] = -0.9f;
  for (int i = 0; i < 8; ++i) {
    a->process(ins, out_a);
    b->process(ins, out_b);
    CHECK(out_a[0] == doctest::Approx(out_b[0]));
    CHECK(out_a[0] == doctest::Approx(0.55f));
  }
}

TEST_CASE("ladder migrates all four stage states") {
  const ModuleInfo* info = find_module("ladder");
  REQUIRE(info != nullptr);
  auto a = info->create();
  auto b = info->create();
  a->init(48000.0, {});
  b->init(48000.0, {});
  float ins[4] = {0.0f, 800.0f, 0.6f, 0.0f};
  float out_a[1] = {0}, out_b[1] = {0};
  for (int i = 0; i < 1000; ++i) {
    ins[0] = (i / 24) % 2 == 0 ? 0.7f : -0.7f;
    a->process(ins, out_a);
  }
  b->copy_state_from(*a);
  for (int i = 1000; i < 1032; ++i) {
    ins[0] = (i / 24) % 2 == 0 ? 0.7f : -0.7f;
    a->process(ins, out_a);
    b->process(ins, out_b);
    CHECK(out_a[0] == doctest::Approx(out_b[0]));
  }
}

TEST_CASE("lpg migrates envelope and filter state mid-pluck") {
  const ModuleInfo* info = find_module("lpg");
  REQUIRE(info != nullptr);
  auto a = info->create();
  auto b = info->create();
  a->init(48000.0, {});
  b->init(48000.0, {});
  float ins[5] = {0.0f, 0.0f, 0.0f, 0.5f, 0.1f};
  float out_a[1], out_b[1];
  ins[2] = 1.0f;  // strike
  a->process(ins, out_a);
  // Decay for a while with audio flowing so env_, lp1_, lp2_ are all nonzero.
  for (int i = 1; i < 400; ++i) {
    ins[0] = (i / 20) % 2 == 0 ? 0.6f : -0.6f;
    a->process(ins, out_a);  // strike stays high: prev_strike_ is 1.0
  }
  b->copy_state_from(*a);
  // Strike held high across the copy: a forgotten prev_strike_ re-plucks b.
  for (int i = 400; i < 432; ++i) {
    ins[0] = (i / 20) % 2 == 0 ? 0.6f : -0.6f;
    a->process(ins, out_a);
    b->process(ins, out_b);
    CHECK(out_a[0] == doctest::Approx(out_b[0]));
  }
}

TEST_CASE("noise migrates rng and pink filter state") {
  const ModuleInfo* info = find_module("noise");
  REQUIRE(info != nullptr);
  auto a = info->create();
  auto b = info->create();
  Config cfg;
  ConfigValue c; c.str = "pink";
  cfg.push_back({"color", c});
  a->init(48000.0, cfg);
  b->init(48000.0, cfg);
  float out_a[1], out_b[1];
  for (int i = 0; i < 500; ++i) a->process(nullptr, out_a);
  b->copy_state_from(*a);
  for (int i = 0; i < 8; ++i) {
    a->process(nullptr, out_a);
    b->process(nullptr, out_b);
    CHECK(out_a[0] == doctest::Approx(out_b[0]));
  }
}

TEST_CASE("quant migrates changed-gate state") {
  const ModuleInfo* info = find_module("quant");
  REQUIRE(info != nullptr);
  auto a = info->create();
  auto b = info->create();
  a->init(48000.0, {});
  b->init(48000.0, {});
  float ins[3] = {0.25f, 0.0f, 0.0f};
  float out_a[2], out_b[2];
  a->process(ins, out_a);  // a: first_ consumed, last_q_ = 3
  b->copy_state_from(*a);
  // Change the degree on the first post-copy sample: `a` fires changed
  // because last_q_ was 3; a fresh last_q_ of 0 would stay silent.
  ins[0] = 0.0f;
  a->process(ins, out_a);
  b->process(ins, out_b);
  CHECK(out_a[1] == doctest::Approx(1.0f));
  CHECK(out_b[1] == doctest::Approx(out_a[1]));
  for (int i = 0; i < 8; ++i) {
    ins[0] = i % 2 == 0 ? 0.25f : 0.5f;
    a->process(ins, out_a);
    b->process(ins, out_b);
    CHECK(out_a[0] == doctest::Approx(out_b[0]));
    CHECK(out_a[1] == doctest::Approx(out_b[1]));
  }
}

// The copy must carry the ring buffer itself (echoes in flight), the write
// head, the slewed delay length, and the feedback-filter states. A short
// delay with feedback keeps energy circulating through ALL of that state at
// the copy point; comparing across the next echo arrival makes the buffer,
// write head, and lp_ copies load-bearing (delay_ and the DC pair are
// defense-in-depth: slew 0 re-snaps delay_ identically).
TEST_CASE("delay migrates ring buffer, write head, and loop filter state") {
  const ModuleInfo* info = find_module("delay");
  REQUIRE(info != nullptr);
  auto a = info->create();
  auto b = info->create();
  a->init(48000.0, {});
  b->init(48000.0, {});
  // in, time, pitch, feedback, mix, tone, slew
  float ins[7] = {0.0f, 0.005f, 0.0f, 0.8f, 1.0f, 3000.0f, 0.0f};
  float out_a[1], out_b[1];
  ins[0] = 1.0f;
  a->process(ins, out_a);
  ins[0] = 0.0f;
  for (int i = 1; i < 700; ++i) {
    ins[0] = i % 190 == 0 ? 0.7f : 0.0f;  // keep fresh energy circulating
    a->process(ins, out_a);
  }
  b->copy_state_from(*a);
  for (int i = 700; i < 1400; ++i) {  // crosses several 240-sample echoes
    ins[0] = i % 190 == 0 ? 0.7f : 0.0f;
    a->process(ins, out_a);
    b->process(ins, out_b);
    CHECK(out_a[0] == doctest::Approx(out_b[0]));
  }
}

TEST_CASE("random migrates rng, phase, and segment endpoints") {
  const ModuleInfo* info = find_module("random");
  REQUIRE(info != nullptr);
  auto a = info->create();
  auto b = info->create();
  Config cfg;
  ConfigValue s; s.num = 4.0; s.is_num = true;
  cfg.push_back({"seed", s});
  a->init(48000.0, cfg);
  b->init(48000.0, cfg);
  const float ins[2] = {200.0f, 1.0f};  // 240-sample segments, smoothing on
  float out_a[2], out_b[2];
  for (int i = 0; i < 500; ++i) a->process(ins, out_a);  // mid-segment
  b->copy_state_from(*a);
  for (int i = 0; i < 480; ++i) {  // crosses at least one clock tick
    a->process(ins, out_a);
    b->process(ins, out_b);
    CHECK(out_a[0] == doctest::Approx(out_b[0]));
    CHECK(out_a[1] == doctest::Approx(out_b[1]));
  }
}

TEST_CASE("adsr migrates an active decay with the gate held high") {
  const ModuleInfo* info = find_module("adsr");
  REQUIRE(info != nullptr);
  auto a = info->create();
  auto b = info->create();
  a->init(1000.0, {});
  b->init(1000.0, {});
  // gate, attack, decay, sustain, release
  float ins[5] = {1.0f, 0.01f, 0.2f, 0.4f, 0.05f};
  float out_a[1], out_b[1];
  for (int i = 0; i < 60; ++i) a->process(ins, out_a);  // mid-decay
  b->copy_state_from(*a);
  // Gate stays high across the copy: a fresh prev_gate_ (0.0) would see a
  // spurious rising edge and restart the attack, diverging immediately.
  for (int i = 0; i < 80; ++i) {
    a->process(ins, out_a);
    b->process(ins, out_b);
    CHECK(out_a[0] == doctest::Approx(out_b[0]));
  }
}

TEST_CASE("adsr migrates a release tail without re-idling early") {
  const ModuleInfo* info = find_module("adsr");
  REQUIRE(info != nullptr);
  auto a = info->create();
  auto b = info->create();
  a->init(1000.0, {});
  b->init(1000.0, {});
  float ins[5] = {1.0f, 0.01f, 0.02f, 0.6f, 0.5f};
  float out_a[1], out_b[1];
  for (int i = 0; i < 50; ++i) a->process(ins, out_a);  // through to sustain
  ins[0] = 0.0f;
  for (int i = 0; i < 100; ++i) a->process(ins, out_a);  // 100/500 through release
  b->copy_state_from(*a);
  // A dropped start_ or progress_ copy makes b's tail restart from the wrong
  // height or the wrong point on the curve.
  for (int i = 0; i < 120; ++i) {
    a->process(ins, out_a);
    b->process(ins, out_b);
    CHECK(out_a[0] == doctest::Approx(out_b[0]));
  }
}

// The reverb's audible state is the arena (all 13 lines), the per-line write
// positions, the damping/bandwidth filters, and the tank LFO phase. Copying
// mid-tail with modulation active makes each of those observable: a missing
// arena copy silences b's tail, a missing pos array shears every line, a
// missing lfo_ copy detunes the modulated allpasses within a few hundred
// samples.
TEST_CASE("reverb migrates tank contents, line positions, and lfo phase") {
  const ModuleInfo* info = find_module("reverb");
  REQUIRE(info != nullptr);
  auto a = info->create();
  auto b = info->create();
  Config cfg;
  ConfigValue depth; depth.num = 60.0; depth.is_num = true;  // strong mod
  cfg.push_back({"mod_depth", depth});
  a->init(48000.0, cfg);
  b->init(48000.0, cfg);
  // in_l, in_r, decay, damp, mix, freeze
  float ins[6] = {0.0f, 0.0f, 0.8f, 0.3f, 1.0f, 0.0f};
  float out_a[2], out_b[2];
  for (int i = 0; i < 24000; ++i) {
    ins[0] = i % 5 == 0 ? 0.6f : -0.2f;
    ins[1] = i % 7 == 0 ? -0.5f : 0.3f;
    a->process(ins, out_a);
  }
  ins[0] = ins[1] = 0.0f;
  b->copy_state_from(*a);
  for (int i = 0; i < 2000; ++i) {
    a->process(ins, out_a);
    b->process(ins, out_b);
    CHECK(out_a[0] == doctest::Approx(out_b[0]));
    CHECK(out_a[1] == doctest::Approx(out_b[1]));
  }
}

TEST_CASE("dcblock migrates state and smoothed cutoff") {
  // dcblock inputs: in_l, in_r, cutoff
  const float ins[3] = {0.8f, -0.5f, 25.0f};
  check_continuity("dcblock", ins, 1000);
}

TEST_CASE("stereo migrates states and smoothed parameters") {
  // stereo inputs: in_l, in_r, width, balance, mono, swap
  const float ins[6] = {0.8f, -0.5f, 1.4f, -0.2f, 0.0f, 0.0f};
  check_continuity("stereo", ins, 1000);
}


