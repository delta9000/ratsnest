#include <doctest/doctest.h>
#include <vector>
#include "engine/controller.h"
#include "engine/rt_runner.h"

using namespace ms;
using engine::Controller;
using engine::RtRunner;

// Single-threaded harness: we play the RT thread by calling render() ourselves.
struct Rig {
  RtRunner run{48000.0};
  Controller ctl{run, 48000.0};
  std::vector<float> l = std::vector<float>(4096), r = std::vector<float>(4096);
  void spin(size_t frames) { run.render(l.data(), r.data(), frames); }
};

TEST_CASE("first load starts playing") {
  Rig rig;
  auto res = rig.ctl.load("a = vca { gain: 1.0 }\n1 -> a.in\na -> out.in\n");
  REQUIRE(res.ok);
  CHECK(res.topology);
  rig.spin(64);
  CHECK(rig.l[0] == doctest::Approx(1.0f));
}

TEST_CASE("param-only reload glides without a swap") {
  Rig rig;
  REQUIRE(rig.ctl.load("a = vca { gain: 0.5 }\n1 -> a.in\na -> out.in\n").ok);
  rig.spin(64);
  auto res = rig.ctl.load("a = vca { gain: 1.0 }\n1 -> a.in\na -> out.in\n");
  REQUIRE(res.ok);
  CHECK(!res.topology);
  rig.spin(4000);  // > 20ms glide
  CHECK(rig.l[3999] == doctest::Approx(1.0f));
  engine::Engine* e = nullptr;
  CHECK(!rig.run.try_retire(e));  // no engine was swapped out
}

TEST_CASE("topology reload swaps, old engine is retired and reaped") {
  Rig rig;
  REQUIRE(rig.ctl.load("a = vca { gain: 1.0 }\n1 -> a.in\na -> out.in\n").ok);
  rig.spin(64);
  CHECK(rig.ctl.engines_in_flight() == 1);
  auto res = rig.ctl.load(
      "a = vca { gain: 1.0 }\nb = lfo\n1 -> a.in\na -> out.in\nb -> out.in : 0\n");
  REQUIRE(res.ok);
  CHECK(res.topology);
  CHECK(rig.ctl.engines_in_flight() == 2);
  rig.spin(4096);  // > 50ms fade -> old engine retired
  rig.ctl.drain_retired();
  CHECK(rig.ctl.engines_in_flight() == 1);
}

TEST_CASE("bad patch reports diagnostics and keeps the last good graph") {
  Rig rig;
  REQUIRE(rig.ctl.load("a = vca { gain: 1.0 }\n1 -> a.in\na -> out.in\n").ok);
  rig.spin(64);
  auto res = rig.ctl.load("a = flanger\n");
  CHECK(!res.ok);
  CHECK(!res.errors.empty());
  rig.spin(64);
  CHECK(rig.l[63] == doctest::Approx(1.0f));  // still playing
  // and a following good reload still works
  CHECK(rig.ctl.load("a = vca { gain: 0.5 }\n1 -> a.in\na -> out.in\n").ok);
}

TEST_CASE("first-load failure leaves silence, then a good load recovers") {
  Rig rig;
  auto res = rig.ctl.load("nonsense = flanger\n");
  CHECK(!res.ok);
  rig.spin(64);
  CHECK(rig.l[0] == 0.0f);
  REQUIRE(rig.ctl.load("a = vca { gain: 1.0 }\n1 -> a.in\na -> out.in\n").ok);
  rig.spin(64);
  CHECK(rig.l[63] == doctest::Approx(1.0f));
}

TEST_CASE("watch resolves names and survives a topology swap") {
  Rig rig;
  REQUIRE(rig.ctl.load("a = vca { gain: 1.0 }\n1 -> a.in\na -> out.in\n").ok);
  REQUIRE(rig.ctl.set_watch("a"));
  rig.spin(64);
  float v = 0.0f;
  REQUIRE(rig.run.try_watch_value(v));
  CHECK(v == doctest::Approx(1.0f));
  CHECK(!rig.ctl.set_watch("ghost"));
  CHECK(!rig.ctl.set_watch("a.nope"));
  // topology change: watch re-applied to the new graph's indices
  REQUIRE(rig.ctl.load(
      "a = vca { gain: 1.0 }\nb = lfo\n1 -> a.in\na -> out.in\nb -> out.in : 0\n").ok);
  rig.spin(4096);
  while (rig.run.try_watch_value(v)) {}
  rig.spin(64);
  REQUIRE(rig.run.try_watch_value(v));
  CHECK(v == doctest::Approx(1.0f));
}

// Voice expansion (spec §3) names instances `<member>#<i>`, so node names
// legitimately contain dots (from the member's own name, e.g. `lead.osc`)
// as well as the `#i` instance suffix. A watch spec must resolve against the
// actual compiled node names (longest node-name prefix, same rule as
// endpoint resolution) rather than splitting on the first dot in the spec.
TEST_CASE("watch resolves dotted, instanced voice member names") {
  Rig rig;
  REQUIRE(rig.ctl.load(
              "poly = alloc { n: 2 }\n"
              "lead = voice { notes: poly }\n"
              "lead.osc = vco { freq: 220hz }\n"
              "lead.osc -> out.in\n")
              .ok);
  // Bare instanced node name: no port suffix, falls back to the first output.
  CHECK(rig.ctl.set_watch("lead.osc#1"));
  rig.spin(64);
  float v = 0.0f;
  REQUIRE(rig.run.try_watch_value(v));
  // Explicit port on a dotted, instanced node name.
  CHECK(rig.ctl.set_watch("lead.osc#1.out"));
  rig.spin(64);
  REQUIRE(rig.run.try_watch_value(v));
}
