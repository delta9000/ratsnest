#include <doctest/doctest.h>
#include <string>
#include "dsp/modules.h"

using namespace ms::dsp;

namespace {
struct FuncRig {
  std::unique_ptr<Module> module;
  float ins[4] = {0.0f, 0.0f, 0.01f, 0.02f};  // trig, in, rise, fall

  explicit FuncRig(const char* curve = "lin", const char* cycle = "off") {
    const ModuleInfo* info = find_module("func");
    REQUIRE(info != nullptr);
    module = info->create();
    Config cfg;
    cfg.push_back({"curve", ConfigValue{curve, 0, {}, false}});
    cfg.push_back({"cycle", ConfigValue{cycle, 0, {}, false}});
    module->init(1000.0, cfg);
  }

  void tick(float (&out)[3]) { module->process(ins, out); }

  void trigger(float (&out)[3]) {
    ins[0] = 1.0f;
    tick(out);
    ins[0] = 0.0f;
  }
};
}  // namespace

TEST_CASE("function generator honors rise and fall times") {
  FuncRig func;
  float out[3];
  func.trigger(out);
  CHECK(out[0] == doctest::Approx(0.1f));
  for (int i = 1; i < 10; ++i) func.tick(out);
  CHECK(out[0] == doctest::Approx(1.0f));
  CHECK(out[1] == doctest::Approx(1.0f));
  CHECK(out[2] == doctest::Approx(0.0f));

  for (int i = 0; i < 20; ++i) func.tick(out);
  CHECK(out[0] == doctest::Approx(0.0f));
  CHECK(out[2] == doctest::Approx(1.0f));
}

TEST_CASE("function completion outputs are one-sample pulses") {
  FuncRig func;
  float out[3];
  func.trigger(out);
  for (int i = 1; i < 10; ++i) func.tick(out);
  REQUIRE(out[1] == doctest::Approx(1.0f));
  func.tick(out);
  CHECK(out[1] == doctest::Approx(0.0f));
  for (int i = 1; i < 20; ++i) func.tick(out);
  REQUIRE(out[2] == doctest::Approx(1.0f));
  func.tick(out);
  CHECK(out[2] == doctest::Approx(0.0f));
}

TEST_CASE("function curves shape progress without changing timing") {
  FuncRig linear("lin");
  FuncRig exponential("exp");
  FuncRig logarithmic("log");
  float lin[3], exp[3], log[3];
  linear.trigger(lin);
  exponential.trigger(exp);
  logarithmic.trigger(log);
  for (int i = 1; i < 5; ++i) {
    linear.tick(lin);
    exponential.tick(exp);
    logarithmic.tick(log);
  }
  CHECK(exp[0] > lin[0]);  // convex RC rise
  CHECK(log[0] > lin[0]);
  for (int i = 5; i < 10; ++i) {
    linear.tick(lin);
    exponential.tick(exp);
    logarithmic.tick(log);
  }
  CHECK(lin[0] == doctest::Approx(1.0f));
  CHECK(exp[0] == doctest::Approx(1.0f));
  CHECK(log[0] == doctest::Approx(1.0f));
}

TEST_CASE("cycle starts another rise after end of cycle") {
  FuncRig func("lin", "on");
  float out[3];
  func.trigger(out);
  for (int i = 1; i < 30; ++i) func.tick(out);
  REQUIRE(out[2] == doctest::Approx(1.0f));
  func.tick(out);
  CHECK(out[0] > 0.0f);
}

// Guards the double-precision progress accumulator: with float accumulation
// the per-sample increment rounds to a no-op once rise * rate exceeds 2^25
// samples, and the envelope freezes short of completion, never firing eor.
TEST_CASE("long rises complete instead of stalling on float precision") {
  FuncRig func;            // rate 1000 Hz
  func.ins[2] = 40000.0f;  // 40e6 samples: past the 2^25 float stall point
  float out[3];
  func.trigger(out);
  const long expected = 40'000'000;
  long fired_at = -1;
  for (long i = 1; i <= expected + 1000; ++i) {
    func.tick(out);
    if (out[1] > 0.5f) {
      fired_at = i;
      break;
    }
  }
  REQUIRE(fired_at > 0);
  CHECK(fired_at >= expected - 1000);
}

TEST_CASE("idle function generator slews its input") {
  FuncRig func;
  func.ins[1] = 1.0f;
  float out[3];
  for (int i = 0; i < 5; ++i) func.tick(out);
  CHECK(out[0] == doctest::Approx(0.5f));
  for (int i = 5; i < 10; ++i) func.tick(out);
  CHECK(out[0] == doctest::Approx(1.0f));

  func.ins[1] = 0.0f;
  for (int i = 0; i < 10; ++i) func.tick(out);
  CHECK(out[0] == doctest::Approx(0.5f));
}

TEST_CASE("func exp fall is an RC decay that lands on zero; quad keeps x^2") {
  FuncRig exponential("exp");
  FuncRig quadratic("quad");
  exponential.ins[2] = quadratic.ins[2] = 0.0f;  // instant rise
  exponential.ins[3] = quadratic.ins[3] = 1.0f;  // 1000-sample fall
  float e[3], q[3];
  exponential.trigger(e);
  quadratic.trigger(q);
  float prev = 1.0f;
  for (int i = 0; i < 500; ++i) {
    exponential.tick(e);
    quadratic.tick(q);
    CHECK(e[0] <= prev);
    prev = e[0];
  }
  CHECK(e[0] == doctest::Approx(0.0307).epsilon(0.01));
  CHECK(q[0] == doctest::Approx(0.75).epsilon(0.01));
  for (int i = 500; i < 1000; ++i) exponential.tick(e);
  CHECK(e[0] == 0.0f);
  CHECK(e[2] == 1.0f);  // end of cycle on time
}
