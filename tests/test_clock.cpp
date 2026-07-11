#include <doctest/doctest.h>
#include "dsp/modules.h"

using namespace ms::dsp;

namespace {
struct ClockRig {
  std::unique_ptr<Module> module;
  float ins[3] = {60.0f, 1.0f, 0.0f};  // bpm, run, reset

  ClockRig() {
    const ModuleInfo* info = find_module("clock");
    REQUIRE(info != nullptr);
    module = info->create();
    module->init(100.0, {});
  }

  void tick(float (&out)[2]) { module->process(ins, out); }
};
}  // namespace

TEST_CASE("clock emits a 50 percent gate at the requested BPM") {
  ClockRig clock;
  int high = 0;
  float out[2];
  for (int i = 0; i < 100; ++i) {
    clock.tick(out);
    if (out[0] > 0.5f) ++high;
    CHECK(out[1] == doctest::Approx(1.0f));
  }
  CHECK(high == 50);
}

TEST_CASE("clock stops low and resumes from held phase") {
  ClockRig clock;
  float out[2];
  for (int i = 0; i < 25; ++i) clock.tick(out);
  clock.ins[1] = 0.0f;
  for (int i = 0; i < 20; ++i) {
    clock.tick(out);
    CHECK(out[0] == doctest::Approx(0.0f));
    CHECK(out[1] == doctest::Approx(0.0f));
  }
  clock.ins[1] = 1.0f;
  int remaining_high = 0;
  for (int i = 0; i < 25; ++i) {
    clock.tick(out);
    if (out[0] > 0.5f) ++remaining_high;
  }
  CHECK(remaining_high == 25);
}

TEST_CASE("clock reset returns to the high half-cycle") {
  ClockRig clock;
  float out[2];
  for (int i = 0; i < 75; ++i) clock.tick(out);
  CHECK(out[0] == doctest::Approx(0.0f));
  clock.ins[2] = 1.0f;
  clock.tick(out);
  CHECK(out[0] == doctest::Approx(1.0f));
}
