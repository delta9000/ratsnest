#include <doctest/doctest.h>
#include "dsp/modules.h"

using namespace ms::dsp;

namespace {
struct DivRig {
  std::unique_ptr<Module> module;
  float ins[3] = {0.0f, 2.0f, 0.0f};  // in, by, reset

  explicit DivRig(float by = 2.0f) {
    const ModuleInfo* info = find_module("div");
    REQUIRE(info != nullptr);
    module = info->create();
    module->init(48000.0, {});
    ins[1] = by;
  }

  float tick(float in) {
    ins[0] = in;
    float out[1];
    module->process(ins, out);
    return out[0];
  }

  float edge() {
    (void)tick(0.0f);
    return tick(1.0f);
  }
};
}  // namespace

TEST_CASE("divide by two alternates state on input rising edges") {
  DivRig div(2.0f);
  CHECK(div.edge() == doctest::Approx(1.0f));
  CHECK(div.edge() == doctest::Approx(0.0f));
  CHECK(div.edge() == doctest::Approx(1.0f));
  CHECK(div.edge() == doctest::Approx(0.0f));
}

TEST_CASE("divide by four holds each half for two input periods") {
  DivRig div(4.0f);
  CHECK(div.edge() == doctest::Approx(1.0f));
  CHECK(div.edge() == doctest::Approx(1.0f));
  CHECK(div.edge() == doctest::Approx(0.0f));
  CHECK(div.edge() == doctest::Approx(0.0f));
}

TEST_CASE("divider rounds and clamps by to a positive integer") {
  DivRig rounded(2.6f);
  CHECK(rounded.edge() == doctest::Approx(1.0f));
  CHECK(rounded.edge() == doctest::Approx(1.0f));
  CHECK(rounded.edge() == doctest::Approx(0.0f));

  DivRig clamped(0.0f);
  CHECK(clamped.edge() == doctest::Approx(1.0f));
  CHECK(clamped.edge() == doctest::Approx(1.0f));
}

TEST_CASE("divider reset clears its output until the next edge") {
  DivRig div(4.0f);
  (void)div.edge();
  (void)div.edge();
  div.ins[2] = 1.0f;
  CHECK(div.tick(0.0f) == doctest::Approx(0.0f));
  div.ins[2] = 0.0f;
  CHECK(div.edge() == doctest::Approx(1.0f));
}

// In a phase-locked tree, a bar-derived reset lands on the same sample as a
// master clock edge. That edge must count as beat zero, not be swallowed.
TEST_CASE("reset coincident with a rising edge starts the cycle at beat zero") {
  DivRig div(4.0f);
  (void)div.edge();
  (void)div.edge();
  (void)div.edge();  // mid-cycle: count_ == 2, gate low
  (void)div.tick(0.0f);
  div.ins[2] = 1.0f;
  CHECK(div.tick(1.0f) == doctest::Approx(1.0f));  // reset + edge: beat 0 high
  div.ins[2] = 0.0f;
  (void)div.tick(0.0f);
  CHECK(div.tick(1.0f) == doctest::Approx(1.0f));  // beat 1 still high
  CHECK(div.edge() == doctest::Approx(0.0f));      // beat 2 low
}
