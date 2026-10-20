#include <doctest/doctest.h>
#include <algorithm>
#include <cmath>
#include <memory>
#include <numbers>
#include <thread>
#include <vector>
#include "dsl/parser.h"
#include "engine/rt_runner.h"
#include "engine/controller.h"
#include "graph/reload.h"

using namespace ms;
using engine::Command;
using engine::RtRunner;

static engine::Engine* make_engine(const char* src, double rate = 48000.0) {
  auto p = dsl::parse(src);
  REQUIRE(p.ok());
  auto c = graph::compile(p.ast, rate);
  REQUIRE(c.ok());
  return new engine::Engine(std::move(c.graph));  // ownership goes to the runner
}

static Command swap_cmd(engine::Engine* e) {
  Command c;
  c.kind = Command::Kind::Swap;
  c.engine = e;
  return c;
}

// DC rigs make crossfade math exactly predictable.
static constexpr const char* kDcHalf = "a = vca { gain: 0.5 }\n1 -> a.in\na -> out.in\n";
static constexpr const char* kDcOne  = "a = vca { gain: 1.0 }\n1 -> a.in\na -> out.in\n";

TEST_CASE("silence before any engine arrives") {
  RtRunner run(48000.0);
  float l[64], r[64];
  for (auto& x : l) x = 42.0f;
  run.render(l, r, 64);
  for (float x : l) CHECK(x == 0.0f);
}

TEST_CASE("adopting the first engine is immediate (no fade from silence)") {
  RtRunner run(48000.0);
  REQUIRE(run.push(swap_cmd(make_engine(kDcOne))));
  float l[8], r[8];
  run.render(l, r, 8);
  CHECK(l[0] == doctest::Approx(1.0f));
}

TEST_CASE("SetParam commands glide on the running engine") {
  RtRunner run(48000.0);
  REQUIRE(run.push(swap_cmd(make_engine(kDcHalf))));
  float l[8], r[8];
  run.render(l, r, 8);
  Command c;
  c.kind = Command::Kind::SetParam;
  c.node = 0;  // topo order of kDcHalf: 'a' first (only source of out)
  c.slot = 2;  // vca gain
  c.value = 1.0f;
  REQUIRE(run.push(c));
  std::vector<float> bl(2048), br(2048);
  run.render(bl.data(), br.data(), 2048);
  const size_t glide = static_cast<size_t>(48000.0 * 0.020);  // 960 samples
  CHECK(bl[0] > 0.5f);
  CHECK(bl[glide / 2] == doctest::Approx(0.75f).epsilon(0.01));
  CHECK(bl[glide] == doctest::Approx(1.0f).epsilon(0.001));
  CHECK(bl[2047] == doctest::Approx(1.0f));
}

TEST_CASE("swap crossfades linearly over 50ms and retires the old engine") {
  RtRunner run(48000.0);
  engine::Engine* first = make_engine(kDcOne);
  REQUIRE(run.push(swap_cmd(first)));
  float l[16], r[16];
  run.render(l, r, 16);

  engine::Engine* second = make_engine(kDcHalf);
  REQUIRE(run.push(swap_cmd(second)));
  const size_t fade = static_cast<size_t>(48000.0 * 0.050);  // 2400 samples
  std::vector<float> bl(fade + 256), br(fade + 256);
  run.render(bl.data(), br.data(), bl.size());

  auto expected = [&](size_t i) {
    const float t = static_cast<float>(i) / static_cast<float>(fade);
    return 1.0f * (1.0f - t) + 0.5f * t;
  };
  CHECK(bl[0] == doctest::Approx(expected(0)).epsilon(0.01));
  CHECK(bl[fade / 2] == doctest::Approx(expected(fade / 2)).epsilon(0.01));
  CHECK(bl[fade / 2] > 1.0f);
  CHECK(bl[fade + 100] == doctest::Approx(0.5f).epsilon(0.001));  // fade done

  engine::Engine* retired = nullptr;
  REQUIRE(run.try_retire(retired));
  CHECK(retired == first);
  delete retired;
  CHECK(!run.try_retire(retired));
}

// Build the second engine the way the controller does: compile, pair
// (name,type) survivors via plan_reload, then wrap and push.
TEST_CASE("migration keeps a vco phase-continuous across a swap") {
  const char* src = "osc = vco { freq: 441hz, wave: sine }\nosc -> out.in\n";
  const char* src2 =
      "osc = vco { freq: 441hz, wave: sine }\nmod = lfo\n"
      "osc -> out.in\nmod -> out.in : 0\n";
  auto shadow = dsl::parse(src);
  REQUIRE(shadow.ok());
  auto shadow_g = graph::compile(shadow.ast, 48000.0);
  REQUIRE(shadow_g.ok());

  RtRunner run(48000.0);
  REQUIRE(run.push(swap_cmd(make_engine(src))));
  std::vector<float> l(1000), r(1000);
  run.render(l.data(), r.data(), 1000);

  auto p2 = dsl::parse(src2);
  REQUIRE(p2.ok());
  auto c2 = graph::compile(p2.ast, 48000.0);
  REQUIRE(c2.ok());
  auto plan = graph::plan_reload(*shadow_g.graph, *c2.graph);
  REQUIRE(plan.topology);
  REQUIRE(run.push(swap_cmd(new engine::Engine(std::move(c2.graph)))));

  std::vector<float> l2(64), r2(64);
  run.render(l2.data(), r2.data(), 64);
  // 441 Hz sine at 48kHz moves at most ~0.058 per sample; with migrated phase
  // and linear fade of two aligned signals, successive samples stay close.
  float prev = l[999];
  for (float x : l2) {
    CHECK(std::abs(x - prev) < 0.12f);  // no click; without migration the
    prev = x;                            // restarted phase jumps by up to ~2.0
  }
  engine::Engine* retired = nullptr;
  std::vector<float> tail_l(4096), tail_r(4096);
  run.render(tail_l.data(), tail_r.data(), 4096);  // let the fade finish
  REQUIRE(run.try_retire(retired));
  delete retired;
}

TEST_CASE("watch taps one value per block") {
  RtRunner run(48000.0);
  REQUIRE(run.push(swap_cmd(make_engine(kDcOne))));
  Command w;
  w.kind = Command::Kind::Watch;
  w.node = 0;  // 'a' vca
  w.slot = 0;  // its out
  REQUIRE(run.push(w));
  float l[64], r[64];
  run.render(l, r, 64);
  run.render(l, r, 64);
  float v = 0.0f;
  REQUIRE(run.try_watch_value(v));
  CHECK(v == doctest::Approx(1.0f));
}

TEST_CASE("meters report a decaying peak") {
  RtRunner run(48000.0);
  REQUIRE(run.push(swap_cmd(make_engine(kDcOne))));
  float l[256], r[256];
  run.render(l, r, 256);
  CHECK(run.peak_l() == doctest::Approx(1.0f).epsilon(0.02));
}

TEST_CASE("threaded stress: commands pushed while rendering (run under san)") {
  RtRunner run(48000.0);
  REQUIRE(run.push(swap_cmd(make_engine(kDcHalf))));
  std::thread producer([&] {
    for (int i = 0; i < 20000; ++i) {
      Command c;
      c.kind = Command::Kind::SetParam;
      c.node = 0;
      c.slot = 2;
      c.value = (i % 2) ? 1.0f : 0.0f;
      while (!run.push(c)) std::this_thread::yield();
    }
  });
  float l[512], r[512];
  for (int b = 0; b < 500; ++b) {
    run.render(l, r, 512);
    for (float x : l) REQUIRE(std::isfinite(x));
  }
  producer.join();
}

TEST_CASE("oversized blocks are processed in chunks") {
  RtRunner run(48000.0);
  REQUIRE(run.push(swap_cmd(make_engine(kDcOne))));
  std::vector<float> l(RtRunner::kMaxBlock + 1000), r(RtRunner::kMaxBlock + 1000);
  run.render(l.data(), r.data(), l.size());
  CHECK(l.back() == doctest::Approx(1.0f));
}

TEST_CASE("observation telemetry follows graph generations and never blocks when unread") {
  ms::engine::RtRunner runner(48000);
  ms::engine::Controller controller(runner,48000);
  REQUIRE(controller.load("1 -> out.in\n").ok);
  ms::engine::Command observe;observe.kind=ms::engine::Command::Kind::Observe;observe.value=1;
  REQUIRE(runner.push(observe));
  std::vector<float> l(4096),r(4096);
  for(int i=0;i<20;++i)runner.render(l.data(),r.data(),l.size());
  CHECK(l.back()==1.f);
  ms::engine::Observation snapshot;
  REQUIRE(runner.try_observe(snapshot));CHECK(snapshot.generation==controller.generation());
  CHECK(snapshot.size==2);CHECK(snapshot.values[0]==1.f);
  while(runner.try_observe(snapshot)){}
  REQUIRE(controller.load("a = vca {}\n2 -> a.in\na -> out.in\n").ok);
  runner.render(l.data(),r.data(),l.size());
  REQUIRE(runner.try_observe(snapshot));CHECK(snapshot.generation==controller.generation());
  CHECK(snapshot.size==3);
  controller.drain_retired();
}
