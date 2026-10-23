#include "dsl/parser.h"
#include "dsp/modules.h"
#include "engine/engine.h"
#include "graph/graph.h"
#include <array>
#include <cmath>
#include <doctest/doctest.h>
#include <limits>
#include <vector>

using namespace ms::dsp;
namespace {
ConfigValue num(double n) {
  ConfigValue v;
  v.num = n;
  v.is_num = true;
  return v;
}
ConfigValue arr(std::initializer_list<double> xs) {
  ConfigValue v;
  v.arr = xs;
  return v;
}
ConfigValue str(const char *s) {
  ConfigValue v;
  v.str = s;
  return v;
}
struct Rig {
  const ModuleInfo *info;
  std::unique_ptr<Module> module;
  std::vector<float> in, out;
  Rig(const char *type, Config cfg = {}, double rate = 1000)
      : info(find_module(type)) {
    REQUIRE(info != nullptr);
    module = info->create();
    module->init(rate, cfg);
    for (const auto &port : info->inputs)
      in.push_back(port.def);
    out.resize(info->outputs.size());
  }
  float &input(const char *name) {
    return in.at(static_cast<size_t>(info->input_index(name)));
  }
  float output(const char *name) {
    return out.at(static_cast<size_t>(info->output_index(name)));
  }
  void tick() { module->process(in.data(), out.data()); }
  void pulse(const char *name = "clock") {
    input(name) = 0;
    tick();
    input(name) = 1;
    tick();
  }
};
} // namespace

TEST_CASE("musical transport swing preserves downbeats and phrase length") {
  Rig r("transport", {{"beats", num(3)}, {"bars", num(2)}});
  r.input("bpm") = 120;
  r.input("swing") = .5f;
  std::vector<int> ticks, beats, bars, phrases;
  for (int i = 0; i < 6000; ++i) {
    r.tick();
    if (r.output("clock") > 0)
      ticks.push_back(i);
    if (r.output("beat") > 0)
      beats.push_back(i);
    if (r.output("bar") > 0)
      bars.push_back(i);
    if (r.output("phrase") > 0)
      phrases.push_back(i);
  }
  REQUIRE(ticks.size() == 48);
  CHECK(ticks[0] == 0);
  CHECK(ticks[1] == 188);
  CHECK(ticks[2] == 250);
  CHECK(ticks[3] == 438);
  CHECK(beats == std::vector<int>{0, 500, 1000, 1500, 2000, 2500, 3000, 3500,
                                  4000, 4500, 5000, 5500});
  CHECK(bars == std::vector<int>{0, 1500, 3000, 4500});
  CHECK(phrases == std::vector<int>{0, 3000});
}
TEST_CASE("musical transport pause resume reset and slow phase preserve time") {
  Rig r("transport");
  r.input("bpm") = 60;
  for (int i = 0; i < 101; ++i)
    r.tick();
  r.input("run") = 0;
  r.tick();
  const float phase = r.output("beat_phase");
  for (int i = 0; i < 1000; ++i) {
    r.tick();
    REQUIRE(r.output("beat_phase") == phase);
    REQUIRE(r.output("clock") == 0.f);
  }
  r.input("run") = 1;
  r.tick();
  CHECK(r.output("clock") == 0.f);
  r.input("reset") = 1;
  r.tick();
  CHECK(r.output("phrase") == 1.f);
  CHECK(r.output("beat_phase") == 0.f);
  r.input("reset") = 0;
  r.input("bpm") = .06f;
  for (int i = 0; i < 500000; ++i)
    r.tick();
  CHECK(r.output("beat_phase") == doctest::Approx(.501).epsilon(.00001));
}
TEST_CASE("musical transport swing changes wait for the next beat") {
  Rig r("transport");
  r.input("bpm") = 60;
  std::vector<int> ticks;
  for (int i = 0; i < 1500; ++i) {
    if (i == 100)
      r.input("swing") = .5f;
    r.tick();
    if (r.output("clock") > 0)
      ticks.push_back(i);
  }
  CHECK(ticks == std::vector<int>{0, 250, 500, 750, 1000, 1375});
}
TEST_CASE("musical bern repeats values independently of density and supports "
          "reroll") {
  Rig r("bern", {{"seed", num(42)}}), other("bern", {{"seed", num(42)}});
  r.input("length") = 8;
  other.input("length") = 8;
  std::array<float, 8> values{};
  for (int i = 0; i < 24; ++i) {
    r.input("prob") = i % 2 ? 1.f : 0.f;
    r.pulse("gate");
    other.pulse("gate");
    CHECK(r.output("value") == other.output("value"));
    if (i < 8)
      values[static_cast<size_t>(i)] = r.output("value");
    else
      CHECK(r.output("value") == values[static_cast<size_t>(i % 8)]);
  }
  r.input("reset") = 1;
  r.tick();
  r.input("reset") = 0;
  r.pulse("gate");
  CHECK(r.output("value") == values[0]);
  r.input("reroll") = 1;
  r.tick();
  r.input("reroll") = 0;
  r.pulse("gate");
  CHECK(r.output("value") != values[0]);
}
TEST_CASE("musical phrase captures a loop then freezes rotates and resets it") {
  Rig r("phrase");
  r.input("length") = 4;
  for (int i = 0; i < 4; ++i) {
    r.input("in") = static_cast<float>(i + 1);
    r.pulse();
  }
  r.input("freeze") = 1;
  r.input("in") = 99;
  r.pulse();
  CHECK(r.output("out") == 1.f);
  CHECK(r.output("tap1") == 4.f);
  CHECK(r.output("eoc") == 1.f);
  r.input("rotate") = 1;
  r.input("transpose") = -1;
  r.tick();
  CHECK(r.output("out") == 1.f);
  r.input("reset") = 1;
  r.tick();
  r.input("reset") = 0;
  r.pulse();
  CHECK(r.output("out") == 1.f);
  r.input("freeze") = 0;
  r.input("rotate") = 0;
  r.pulse();
  CHECK(r.output("out") == 98.f);
}
TEST_CASE("musical scenes queue cancel hold and restart on bar boundaries") {
  Rig r("scene", {{"bars", arr({2, 3, 1})}});
  r.pulse();
  CHECK(r.output("index") == 0.f);
  CHECK(r.output("trig") == 1.f);
  r.input("request") = 2;
  r.tick();
  CHECK(r.output("pending") == 2.f);
  CHECK(r.output("index") == 0.f);
  r.pulse();
  CHECK(r.output("index") == 2.f);
  CHECK(r.output("pending") == -1.f);
  r.input("hold") = 1;
  r.pulse();
  CHECK(r.output("index") == 2.f);
  CHECK(r.output("trig") == 0.f);
  r.input("launch") = 1;
  r.tick();
  CHECK(r.output("pending") == 2.f);
  r.pulse();
  CHECK(r.output("trig") == 1.f);
  r.input("request") = 1;
  r.tick();
  r.input("request") = -1;
  r.tick();
  CHECK(r.output("pending") == -1.f);
  r.input("hold") = 0;
  r.pulse();
  CHECK(r.output("index") == 0.f);
  r.pulse();
  CHECK(r.output("index") == 0.f);
  r.pulse();
  CHECK(r.output("index") == 1.f);
}
TEST_CASE(
    "musical quant holds harmony until a clock and transposes scale degrees") {
  Rig r("quant",
        {{"scale", arr({0, 2, 3, 5, 7, 8, 10})}, {"mode", str("clock")}});
  r.input("in") = 2.f / 12;
  r.pulse();
  CHECK(r.output("out") == doctest::Approx(2.f / 12));
  r.input("degree") = 1;
  r.input("root") = 2;
  r.tick();
  CHECK(r.output("out") == doctest::Approx(2.f / 12));
  r.pulse();
  CHECK(r.output("out") == doctest::Approx(4.f / 12));
  CHECK(r.output("changed") == 1.f);
  r.input("mask") = 0;
  r.input("in") = 2;
  r.pulse();
  CHECK(r.output("out") == doctest::Approx(4.f / 12));
  r.input("mask") = 4095;
  r.input("root") = 0;
  r.input("in") = 0;
  r.input("degree") = -1;
  r.pulse();
  CHECK(r.output("out") == doctest::Approx(-2.f / 12));
}
TEST_CASE("musical quant detects root-only pitch changes") {
  Rig r("quant", {{"scale", arr({0})}});
  r.tick();
  r.input("root") = 1;
  r.tick();
  CHECK(r.output("changed") == 1.f);
}
TEST_CASE("musical channel has unity center and measurable dB gain with pre "
          "post sends") {
  Rig post("channel"), pre("channel", {{"send_mode", str("pre")}});
  for (auto *r : {&post, &pre}) {
    r->input("in_l") = 1;
    r->input("in_r") = .5f;
    r->input("level") = -6;
    r->input("send") = -6;
    for (int i = 0; i < 5000; ++i)
      r->tick();
    CHECK(r->output("out_l") == doctest::Approx(std::pow(10.f, -6.f / 20)));
    CHECK(r->output("peak") == doctest::Approx(-6));
    CHECK(r->output("rms") == doctest::Approx(-8.0412).epsilon(.0001));
  }
  CHECK(post.output("send_l") == doctest::Approx(std::pow(10.f, -12.f / 20)));
  CHECK(pre.output("send_l") == doctest::Approx(std::pow(10.f, -6.f / 20)));
  pre.input("mute") = 1;
  for (int i = 0; i < 1000; ++i)
    pre.tick();
  CHECK(pre.output("send_l") == 0.f);
}
TEST_CASE("musical modules preserve live state through migration") {
  for (const char *type :
       {"transport", "phrase", "scene", "channel", "bern", "quant"}) {
    CAPTURE(type);
    Config cfg;
    if (std::string(type) == "scene")
      cfg = {{"bars", arr({2, 3})}};
    Rig a(type, cfg), b(type, cfg);
    for (int i = 0; i < 3500; ++i) {
      for (const char *port : {"clock", "gate"})
        if (a.info->input_index(port) >= 0)
          a.input(port) = i % 7 == 0 ? 1.f : 0.f;
      a.tick();
    }
    b.module->copy_state_from(*a.module);
    for (int i = 0; i < 1500; ++i) {
      for (const char *port : {"clock", "gate"})
        if (a.info->input_index(port) >= 0)
          a.input(port) = i % 7 == 0 ? 1.f : 0.f;
      b.in = a.in;
      a.tick();
      b.tick();
      REQUIRE(a.out == b.out);
    }
  }
}
TEST_CASE("musical configuration rejects ambiguous timing and scales") {
  for (const char *patch :
       {"t = transport {division: 3}", "t = transport {beats: 0}",
        "s = scene {bars: [2, 1.5]}", "q = quant {scale: [0, 2.5]}",
        "s = scene {}"}) {
    auto parsed = ms::dsl::parse(patch);
    REQUIRE(parsed.ok());
    CHECK_FALSE(ms::graph::compile(parsed.ast, 48000).ok());
  }
}

TEST_CASE("musical scene requests bypass glides and commit together on the "
          "next bar") {
  const auto parsed = ms::dsl::parse(R"(
    time = transport {bpm: 120}
    form = scene {bars: [4,4,4]}
    time.bar -> form.clock
    form.index -> out.left
    form.pending -> out.right
  )");
  auto compiled = ms::graph::compile(parsed.ast, 1000);
  REQUIRE(compiled.ok());
  ms::engine::Engine engine(std::move(compiled.graph));
  size_t node = 0;
  for (size_t i = 0; i < engine.graph().nodes.size(); ++i)
    if (engine.graph().nodes[i].name == "form")
      node = i;
  float left = 0, right = 0;
  engine.process(&left, &right, 1);
  engine.set_param(node, 1, 2, 20);
  engine.process(&left, &right, 1);
  CHECK(left == 0.f);
  CHECK(right == 2.f);
  for (int i = 2; i < 2000; ++i)
    engine.process(&left, &right, 1);
  CHECK(left == 0.f);
  engine.process(&left, &right, 1);
  CHECK(left == 2.f);
  CHECK(right == -1.f);
}
