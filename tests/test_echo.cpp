#include <doctest/doctest.h>
#include <cmath>
#include <utility>
#include "dsp/modules.h"

using namespace ms::dsp;

namespace {
// in_l, in_r, time, feedback, mix, tone, spread, slew
struct Rig {
  std::unique_ptr<Module> m;
  float ins[8] = {0.0f, 0.0f, 0.01f, 0.5f, 1.0f, 20000.0f, 0.0f, 0.0f};
  float out[2] = {0.0f, 0.0f};
  explicit Rig(const char* mode) {
    const ModuleInfo* info = find_module("echo");
    REQUIRE(info != nullptr);
    m = info->create();
    m->init(48000.0, {{"mode", ConfigValue{mode, 0.0, {}, false}}});
  }
  // Index of the loudest |sample| on each side over `n` samples after an impulse.
  std::pair<int, int> peaks(float l, float r, int n) {
    int pl = -1, pr = -1;
    float ml = 0.0f, mr = 0.0f;
    for (int i = 0; i < n; ++i) {
      ins[0] = i == 0 ? l : 0.0f;
      ins[1] = i == 0 ? r : 0.0f;
      m->process(ins, out);
      if (std::abs(out[0]) > ml) { ml = std::abs(out[0]); pl = i; }
      if (std::abs(out[1]) > mr) { mr = std::abs(out[1]); pr = i; }
    }
    return {pl, pr};
  }
};
}  // namespace

TEST_CASE("echo registers stereo ports and a mode") {
  const ModuleInfo* info = find_module("echo");
  REQUIRE(info != nullptr);
  CHECK(info->input_index("in_r") == 1);
  CHECK(info->input_index("spread") == 6);
  CHECK(info->output_index("out_r") == 1);
  CHECK(info->config("mode") != nullptr);
}

TEST_CASE("echo ping: repeats alternate left then right") {
  Rig r("ping");
  const auto [pl, pr] = r.peaks(1.0f, 1.0f, 1500);  // 10 ms time
  CHECK(std::abs(pl - 480) <= 2);   // first repeat, left, at `time`
  CHECK(std::abs(pr - 960) <= 2);   // second, right, at twice `time`
}

TEST_CASE("echo stereo: each side echoes itself, spread delays the right") {
  Rig r("stereo");
  r.ins[3] = 0.0f;   // one repeat each
  r.ins[6] = 0.5f;   // right at 1.5 x time
  const auto [pl, pr] = r.peaks(1.0f, -1.0f, 1500);
  CHECK(std::abs(pl - 480) <= 2);
  CHECK(std::abs(pr - 720) <= 2);
  Rig lonly("stereo");
  lonly.ins[3] = 0.0f;
  double rr = 0.0;
  for (int i = 0; i < 2000; ++i) {
    lonly.ins[0] = i == 0 ? 1.0f : 0.0f;
    lonly.m->process(lonly.ins, lonly.out);
    rr += std::abs(lonly.out[1]);
  }
  CHECK(rr == 0.0);  // no crosstalk
}

// Non-finite inputs are swept registry-wide (test_module_hostile.cpp).
TEST_CASE("echo: mix 0 is dry; hot, out-of-range inputs stay finite") {
  Rig dry("ping");
  dry.ins[4] = 0.0f;
  for (int i = 0; i < 100; ++i) {
    dry.ins[0] = std::sin(0.1f * static_cast<float>(i));
    dry.ins[1] = -dry.ins[0];
    dry.m->process(dry.ins, dry.out);
    CHECK(dry.out[0] == doctest::Approx(dry.ins[0]));
    CHECK(dry.out[1] == doctest::Approx(dry.ins[1]));
  }
  Rig r("ping");
  for (int i = 0; i < 200000; ++i) {
    r.ins[0] = i % 2 ? 1e3f : -1.0f;
    r.ins[1] = i % 7 ? -1e3f : 1.0f;
    r.ins[2] = i % 3 ? -5.0f : 50.0f;
    r.ins[3] = 5.0f;
    r.ins[6] = i % 5 ? -2.0f : 9.0f;
    r.m->process(r.ins, r.out);
    REQUIRE(std::isfinite(r.out[1]));
  }
}

TEST_CASE("echo: hot reload carries the repeats") {
  Rig a("ping"), b("ping");
  a.ins[0] = a.ins[1] = 1.0f;
  a.m->process(a.ins, a.out);
  a.ins[0] = a.ins[1] = 0.0f;
  for (int i = 0; i < 100; ++i) a.m->process(a.ins, a.out);
  b.m->copy_state_from(*a.m);
  for (int i = 0; i < 1000; ++i) {
    a.m->process(a.ins, a.out);
    b.m->process(a.ins, b.out);
    REQUIRE(b.out[0] == a.out[0]);
    REQUIRE(b.out[1] == a.out[1]);
  }
}
