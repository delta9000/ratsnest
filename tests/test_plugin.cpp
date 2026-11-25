// The `plugin` module hosting a real CLAP plugin: tests/plugin/test_tone.cpp,
// built as a .clap beside this binary (RATSNEST_TEST_CLAP).
#include <doctest/doctest.h>
#include <string>
#include <vector>
#include "dsp/clap_host.h"
#include "dsp/modules.h"
#include "graph/subs.h"

using namespace ms::dsp;

namespace {

constexpr size_t kIns = 13;  // in_l in_r gate voct vel p1..p8
constexpr size_t kLatency = 64;

ConfigValue text(const std::string& s) { return {s, 0.0, {}, false}; }

std::unique_ptr<Module> make(Config cfg) {
  const ModuleInfo* info = find_module("plugin");
  REQUIRE(info != nullptr);
  auto m = info->create();
  m->init(48000.0, cfg);
  return m;
}

Config tone(std::vector<std::string> params = {"Gain"}) {
  ConfigValue ps;
  ps.strs = std::move(params);
  return {{"file", text(RATSNEST_TEST_CLAP)}, {"params", ps}};
}

struct Run {
  std::vector<float> l, r;
};

// n samples with fixed inputs, except gate, which `gate_at` raises.
Run run(Module& m, std::vector<float> ins, size_t n, size_t gate_at = SIZE_MAX) {
  ins.resize(kIns, 0.0f);
  Run out;
  float o[2];
  for (size_t i = 0; i < n; ++i) {
    if (i == gate_at) ins[2] = 1.0f;
    m.process(ins.data(), o);
    out.l.push_back(o[0]);
    out.r.push_back(o[1]);
  }
  return out;
}

}  // namespace

TEST_CASE("plugin: with no file it is silent and cheap") {
  auto m = make({});
  const Run r = run(*m, {0.5f, 0.5f}, 200);
  for (float v : r.l) CHECK(v == 0.0f);
  CHECK(find_module("plugin")->wants_midi);
}

TEST_CASE("plugin: audio passes through with one block of latency; p1 drives the param") {
  std::vector<float> ins(kIns, 0.0f);
  ins[0] = 0.4f;
  ins[1] = -0.2f;
  ins[5] = 0.5f;  // p1 = 0.5 of Gain's 0..2 range: unity
  auto m = make(tone());
  Run r = run(*m, ins, 4 * kLatency);
  for (size_t i = 0; i < kLatency; ++i) CHECK(r.l[i] == 0.0f);
  CHECK(r.l[3 * kLatency] == doctest::Approx(0.4f));
  CHECK(r.r[3 * kLatency] == doctest::Approx(-0.2f));

  ins[5] = 0.25f;  // Gain 0.5
  r = run(*m, ins, 4 * kLatency);
  CHECK(r.l.back() == doctest::Approx(0.2f));
}

TEST_CASE("plugin: a gate plays voct's note on its sample, one block late") {
  std::vector<float> ins(kIns, 0.0f);
  ins[3] = 0.25f;  // three semitones over A3: MIDI 60
  ins[4] = 0.5f;
  ins[5] = 0.5f;
  auto m = make(tone());
  const Run r = run(*m, ins, 3 * kLatency, kLatency + 10);
  CHECK(r.l[2 * kLatency + 9] == 0.0f);
  CHECK(r.l[2 * kLatency + 10] == doctest::Approx(0.5f));
  CHECK(r.r[2 * kLatency + 10] == doctest::Approx(60.0f / 127.0f));
}

TEST_CASE("plugin: keyboard MIDI reaches it only with midi: on") {
  std::vector<float> ins(kIns, 0.0f);
  ins[5] = 0.5f;
  const uint8_t on[3] = {0x90, 64, 127};
  for (const char* midi : {"off", "on"}) {
    Config cfg = tone();
    cfg.emplace_back("midi", text(midi));
    auto m = make(cfg);
    m->on_midi(on, 3);
    const Run r = run(*m, ins, 2 * kLatency);
    CHECK(r.l.back() == doctest::Approx(std::string(midi) == "on" ? 1.0f : 0.0f));
  }
}

TEST_CASE("plugin: a missing plugin or unknown param is silent, not fatal") {
  std::vector<float> ins(kIns, 0.0f);
  ins[0] = 0.4f;
  auto missing = make({{"file", text("/nonexistent/None.clap")}});
  for (float v : run(*missing, ins, 3 * kLatency).l) CHECK(v == 0.0f);
  auto bad = make(tone({"Cutoff"}));
  for (float v : run(*bad, ins, 3 * kLatency).l) CHECK(v == 0.0f);
}

TEST_CASE("plugin: params by name or id, through a parsed patch") {
  const std::string src = std::string("p = plugin { file: \"") + RATSNEST_TEST_CLAP +
                          "\", params: [\"Gain\", \"3\"], p1: 0.5 }\np -> out.in\n";
  CHECK(ms::graph::build_patch(src, "", 48000.0).compiled.ok());
  const std::string bad = "p = plugin { params: 3 }\np -> out.in\n";
  CHECK_FALSE(ms::graph::build_patch(bad, "", 48000.0).compiled.ok());
}

TEST_CASE("plugin: the library's params read back with ranges") {
  std::string err;
  const auto* f = clap::factory(RATSNEST_TEST_CLAP, err);
  REQUIRE(f != nullptr);
  const clap_plugin_t* p = f->create_plugin(f, clap::host(), "org.ratsnest.test.tone");
  REQUIRE(p->init(p));
  const auto ps = clap::params(p);
  REQUIRE(ps.size() == 1);
  CHECK(ps[0].name == "Gain");
  CHECK(ps[0].max == 2.0);
  p->destroy(p);
}
