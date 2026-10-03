#include <doctest/doctest.h>
#include <cmath>
#include "dsp/modules.h"

using namespace ms::dsp;

namespace {
std::unique_ptr<Module> make_random(double seed) {
  const ModuleInfo* info = find_module("random");
  REQUIRE(info != nullptr);
  auto m = info->create();
  Config cfg;
  ConfigValue s; s.num = seed; s.is_num = true;
  cfg.push_back({"seed", s});
  m->init(48000.0, cfg);
  return m;
}
}  // namespace

TEST_CASE("random stepped output changes at the clock rate and stays bounded") {
  auto m = make_random(3.0);
  float ins[2] = {100.0f, 0.0f};  // 100 Hz, no smoothing
  float out[2];
  int changes = 0;
  float prev = 0.0f;
  for (int i = 0; i < 48000; ++i) {
    m->process(ins, out);
    CHECK(std::abs(out[1]) <= 1.0f);
    CHECK(out[0] == doctest::Approx(out[1]));  // smooth 0: out == stepped
    if (i > 0 && out[1] != prev) ++changes;
    prev = out[1];
  }
  CHECK(changes >= 95);
  CHECK(changes <= 105);
}

TEST_CASE("random is deterministic per seed") {
  auto a = make_random(5.0);
  auto b = make_random(5.0);
  auto c = make_random(6.0);
  float ins[2] = {500.0f, 0.0f};
  float oa[2], ob[2], oc[2];
  bool differs = false;
  for (int i = 0; i < 4800; ++i) {
    a->process(ins, oa);
    b->process(ins, ob);
    c->process(ins, oc);
    CHECK(oa[1] == ob[1]);
    if (oa[1] != oc[1]) differs = true;
  }
  CHECK(differs);
}

TEST_CASE("random smooth output is continuous") {
  auto m = make_random(9.0);
  float ins[2] = {50.0f, 1.0f};  // full smoothing
  float out[2];
  m->process(ins, out);
  float prev = out[0];
  float max_jump = 0.0f;
  for (int i = 1; i < 48000; ++i) {
    m->process(ins, out);
    max_jump = std::max(max_jump, std::abs(out[0] - prev));
    prev = out[0];
  }
  // 50 Hz segments at 48 kHz = 960 samples; a cosine ramp over ±2 range
  // moves at most ~2*pi/960 per sample. Allow generous margin.
  CHECK(max_jump < 0.02f);
}

TEST_CASE("slow random reaches the next target without freezing") {
  auto slow = make_random(9.0);
  auto fast = make_random(9.0);
  float ins[2] = {0.001f, 1.0f};
  float out[2], reference[2];
  slow->process(ins, out);
  const float first = out[1];
  float fast_ins[2] = {48000.0f, 1.0f};
  fast->process(fast_ins, reference);
  REQUIRE(reference[1] != first);
  for (int i = 1; i < 48000000; ++i) slow->process(ins, out);
  CHECK(out[1] == reference[1]);
  CHECK(out[0] == doctest::Approx(first).epsilon(0.00001));
}
