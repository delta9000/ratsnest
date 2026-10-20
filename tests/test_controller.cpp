#include <doctest/doctest.h>
#include <cstdio>
#include <filesystem>
#include <fstream>
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

// Regression: a topology save during the 50 ms crossfade used to be rejected
// ("reload still in flight"), losing the edit until the next save. It is now
// held -- newest only -- and applied once the fading engine is reaped.
TEST_CASE("topology saves during a crossfade coalesce and apply after the fade") {
  Rig rig;
  REQUIRE(rig.ctl.load("a = vca { gain: 1.0 }\n1 -> a.in\na -> out.in\n").ok);
  rig.spin(64);
  REQUIRE(rig.ctl.load(
      "a = vca { gain: 1.0 }\nb = lfo\n1 -> a.in\na -> out.in\nb -> out.in : 0\n").topology);
  auto held = rig.ctl.load(
      "a = vca { gain: 0.3 }\nc = noise\n1 -> a.in\na -> out.in\nc -> out.in : 0\n");
  CHECK(!held.ok);
  CHECK(held.deferred);
  CHECK(held.errors.empty());
  auto newest = rig.ctl.load("a = vca { gain: 0.7 }\nd = lfo\n1 -> a.in\na -> out.in\n"
                             "d -> out.in : 0\n");
  CHECK(newest.deferred);
  CHECK(rig.ctl.has_pending());
  CHECK(!rig.ctl.apply_pending());  // still fading: nothing applied
  CHECK(rig.ctl.engines_in_flight() == 2);

  rig.spin(4096);  // fade done, old engine retired
  auto applied = rig.ctl.apply_pending();
  REQUIRE(applied);
  CHECK(applied->ok);
  CHECK(applied->topology);
  CHECK(!rig.ctl.has_pending());
  CHECK(!rig.ctl.apply_pending());  // applied once
  rig.spin(4096);
  rig.spin(64);
  CHECK(rig.l[63] == doctest::Approx(0.7f));  // the newest held edit won
}

TEST_CASE("a later applied edit supersedes a held one") {
  Rig rig;
  REQUIRE(rig.ctl.load("a = vca { gain: 1.0 }\n1 -> a.in\na -> out.in\n").ok);
  rig.spin(64);
  const char* b = "a = vca { gain: 1.0 }\nb = lfo\n1 -> a.in\na -> out.in\nb -> out.in : 0\n";
  REQUIRE(rig.ctl.load(b).topology);
  CHECK(rig.ctl.load("a = vca { gain: 0.3 }\n1 -> a.in\na -> out.in\n").deferred);
  // Params-only relative to the live graph: applies now, so the held edit is stale.
  auto p = rig.ctl.load(
      "a = vca { gain: 0.6 }\nb = lfo\n1 -> a.in\na -> out.in\nb -> out.in : 0\n");
  REQUIRE(p.ok);
  CHECK(!p.topology);
  CHECK(!rig.ctl.has_pending());
  rig.spin(4096);
  CHECK(!rig.ctl.apply_pending());
  rig.spin(64);
  CHECK(rig.l[63] == doctest::Approx(0.6f));
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

TEST_CASE("controller expands subs relative to the patch path") {
  namespace fs = std::filesystem;
  const fs::path dir = fs::temp_directory_path() / "ms_sub_ctl";
  fs::create_directories(dir / "lib");
  {
    std::ofstream f(dir / "lib" / "tone.sub");
    f << "tune = param { default: 220 }\nmain = output {}\n"
         "osc = vco { wave: sine, freq: tune }\nosc -> main.in\n";
  }
  ms::engine::RtRunner runner(48000.0);
  ms::engine::Controller ctl(runner, 48000.0);
  auto res = ctl.load(
      "t = sub { file: \"lib/tone.sub\", tune: 440 }\nt.main -> out.in\n",
      (dir / "song.patch").string());
  REQUIRE(res.ok);
  REQUIRE(res.files.size() == 1);
  CHECK(res.files[0] == (dir / "lib" / "tone.sub").lexically_normal().string());
  fs::remove_all(dir);
}

TEST_CASE("full control queue rejects a whole parameter edit without partial application") {
  Rig rig;
  REQUIRE(rig.ctl.load("a = vca { gain: 0.5 }\n1 -> a.in\na -> out.in\n").ok);
  rig.spin(64);
  engine::Command command;command.kind=engine::Command::Kind::Watch;command.node=RtRunner::kWatchOff;
  while(rig.run.push(command)) {}
  CHECK_FALSE(rig.ctl.load("a = vca { gain: 0.8 }\n1 -> a.in\na -> out.in\n").ok);
  rig.spin(4000);
  CHECK(rig.l[3999]==doctest::Approx(.5));
  REQUIRE(rig.ctl.load("a = vca { gain: 0.8 }\n1 -> a.in\na -> out.in\n").ok);
  rig.spin(4000);
  CHECK(rig.l[3999]==doctest::Approx(.8));
}
