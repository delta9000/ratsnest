#include <string>
#include <vector>
#include <doctest/doctest.h>
#include "dsp/modules.h"
#include "bank_conformance.h"

using namespace ms::dsp;
using ms::dsp::test::check_bank_conformance;

namespace {
constexpr double kRate = 48000.0;

ConfigValue number(double n) {
  ConfigValue v;
  v.num = n;
  v.is_num = true;
  return v;
}

// Drives a `chord` instance by port name.
struct ChordRig {
  const ModuleInfo* info;
  std::unique_ptr<Module> module;
  std::vector<float> ins;
  std::vector<float> outs;

  explicit ChordRig(int n) {
    info = find_module("chord");
    REQUIRE(info != nullptr);
    module = info->create();
    Config cfg = {{"n", number(n)}};
    module->init(kRate, cfg);
    for (const auto& in : info->inputs) ins.push_back(in.def);
    outs.assign(info->outputs.size(), 0.f);
  }
  void set(const std::string& name, float v) {
    const int idx = info->input_index(name);
    REQUIRE(idx >= 0);
    ins[static_cast<size_t>(idx)] = v;
  }
  void step(int samples = 1) {
    for (int s = 0; s < samples; ++s) module->process(ins.data(), outs.data());
  }
  float out(const std::string& name) const {
    const int idx = info->output_index(name);
    REQUIRE(idx >= 0);
    return outs[static_cast<size_t>(idx)];
  }
  float gate(int i) const { return out("gate" + std::to_string(i)); }
  float voct(int i) const { return out("voct" + std::to_string(i)); }
};
}  // namespace

TEST_CASE("chord exposes the alloc bank outputs and a pitch per voice") {
  const ModuleInfo* chord = find_module("chord");
  const ModuleInfo* alloc = find_module("alloc");
  REQUIRE(chord != nullptr);
  REQUIRE(alloc != nullptr);
  REQUIRE(chord->outputs.size() == alloc->outputs.size());
  for (size_t i = 0; i < chord->outputs.size(); ++i)
    CHECK(std::string(chord->outputs[i].name) == alloc->outputs[i].name);
  for (int i = 1; i <= 8; ++i) CHECK(chord->input_index("p" + std::to_string(i)) >= 0);
  CHECK(chord->inputs[static_cast<size_t>(chord->input_index("vel"))].def == 1.f);
}

TEST_CASE("chord: every voice sounds its pitch on one gate") {
  ChordRig m(3);
  m.set("p1", 0.f);
  m.set("p2", 0.25f);
  m.set("p3", 0.5833f);
  m.set("p4", 9.f);  // beyond n: ignored
  m.set("vel", 0.7f);
  m.set("gate", 1.f);
  m.step();
  for (int i = 1; i <= 3; ++i) {
    CAPTURE(i);
    CHECK(m.gate(i) == 1.f);
    CHECK(m.out("vel" + std::to_string(i)) == doctest::Approx(0.7f));
  }
  CHECK(m.voct(1) == 0.f);
  CHECK(m.voct(2) == doctest::Approx(0.25f));
  CHECK(m.voct(3) == doctest::Approx(0.5833f));
  CHECK(m.out("spread1") == -1.f);
  CHECK(m.out("spread2") == 0.f);
  CHECK(m.out("spread3") == 1.f);
  for (int i = 4; i <= 8; ++i) {
    CAPTURE(i);
    CHECK(m.gate(i) == 0.f);
    CHECK(m.voct(i) == 0.f);
    CHECK(m.out("vel" + std::to_string(i)) == 0.f);
  }
  // vel latches at the rise.
  m.set("vel", 0.2f);
  m.step();
  CHECK(m.out("vel1") == doctest::Approx(0.7f));
}

TEST_CASE("chord: a slur moves pitch without a retrigger; release holds it") {
  ChordRig m(2);
  m.set("p1", 0.1f);
  m.set("gate", 1.f);
  m.step(10);
  m.set("p1", 0.3f);  // slur: gate stays high
  m.step();
  CHECK(m.gate(1) == 1.f);
  CHECK(m.voct(1) == doctest::Approx(0.3f));
  m.set("gate", 0.f);
  m.step();
  m.set("p1", -2.f);  // the source moves while released
  m.step(5);
  CHECK(m.gate(1) == 0.f);
  CHECK(m.voct(1) == doctest::Approx(0.3f));  // release tail keeps pitch
  m.set("gate", 1.f);
  m.step();
  CHECK(m.voct(1) == doctest::Approx(-2.f));
}

TEST_CASE("chord: a one-sample gate drop retriggers every voice") {
  ChordRig m(4);
  m.set("gate", 1.f);
  m.step(100);
  m.set("gate", 0.f);
  m.step();
  for (int i = 1; i <= 4; ++i) CHECK(m.gate(i) == 0.f);
  m.set("vel", 0.4f);
  m.set("gate", 1.f);
  m.step();
  for (int i = 1; i <= 4; ++i) {
    CAPTURE(i);
    CHECK(m.gate(i) == 1.f);
    CHECK(m.out("vel" + std::to_string(i)) == doctest::Approx(0.4f));
  }
}

// First sample (0-based, from the gate rise) at which each voice's gate is high.
std::vector<int> rise_times(ChordRig& m, int n, int samples) {
  std::vector<int> at(static_cast<size_t>(n), -1);
  m.set("gate", 1.f);
  for (int s = 0; s < samples; ++s) {
    m.step();
    for (int i = 0; i < n; ++i)
      if (at[static_cast<size_t>(i)] < 0 && m.gate(i + 1) == 1.f) at[static_cast<size_t>(i)] = s;
  }
  return at;
}

TEST_CASE("chord: strum delays voice i's rise by i x strum") {
  SUBCASE("up-strum") {
    ChordRig m(4);
    m.set("strum", 0.01f);  // 480 samples per voice
    const auto at = rise_times(m, 4, 2000);
    CHECK(at[0] == 0);
    CHECK(at[1] == 480);
    CHECK(at[2] == 960);
    CHECK(at[3] == 1440);
  }
  SUBCASE("down-strum plays the top voice first") {
    ChordRig m(3);
    m.set("strum", -0.01f);
    const auto at = rise_times(m, 3, 2000);
    CHECK(at[2] == 0);
    CHECK(at[1] == 480);
    CHECK(at[0] == 960);
  }
  SUBCASE("strum clamps at 0.5 s per voice") {
    ChordRig m(2);
    m.set("strum", 9.f);
    const auto at = rise_times(m, 2, 30000);
    CHECK(at[1] == 24000);
  }
  SUBCASE("a gate shorter than the strum never sounds the late voices") {
    ChordRig m(3);
    m.set("strum", 0.01f);
    m.set("p3", 0.5f);
    m.set("gate", 1.f);
    m.step(600);
    CHECK(m.gate(2) == 1.f);
    CHECK(m.gate(3) == 0.f);
    CHECK(m.voct(3) == 0.f);  // not yet strummed: still the old pitch
    m.set("gate", 0.f);
    m.step(2000);
    CHECK(m.gate(3) == 0.f);
  }
}

// Holds the gate for `samples`, setting `strum` from strum_at(s) before each
// sample, and counts each voice's gate rises and falls.
template <class F>
void count_edges(ChordRig& m, int samples, int* rises, int* falls, const F& strum_at) {
  float prev[4] = {};
  m.set("gate", 1.f);
  for (int s = 0; s < samples; ++s) {
    m.set("strum", strum_at(s));
    m.step();
    for (int i = 0; i < 4; ++i) {
      const float g = m.gate(i + 1);
      rises[i] += g > prev[i];
      falls[i] += g < prev[i];
      prev[i] = g;
    }
  }
}

TEST_CASE("chord: a sounding voice stays open while strum moves mid-hold") {
  int rises[4] = {}, falls[4] = {};
  SUBCASE("strum ramped up while held") {
    ChordRig m(4);
    // 0 -> 0.5 s over the hold: every voice opens at once, then strum grows.
    count_edges(m, 48000, rises, falls, [](int s) { return static_cast<float>(s) / 96000.f; });
    for (int i = 0; i < 4; ++i) CHECK(m.gate(i + 1) == 1.f);
  }
  SUBCASE("strum sign flipped while held") {
    ChordRig m(4);
    // Up-strum 10 ms per voice; once voice 2 is open, flip to a down-strum.
    count_edges(m, 4000, rises, falls, [](int s) { return s < 600 ? 0.01f : -0.01f; });
  }
  for (int i = 0; i < 4; ++i) {
    CAPTURE(i);
    CHECK(rises[i] == 1);
    CHECK(falls[i] == 0);
  }
}

TEST_CASE("chord: the gate falling still closes latched voices") {
  ChordRig m(2);
  m.set("gate", 1.f);
  m.step(10);
  m.set("strum", 0.4f);
  m.step(10);
  CHECK(m.gate(2) == 1.f);
  m.set("gate", 0.f);
  m.step();
  CHECK(m.gate(1) == 0.f);
  CHECK(m.gate(2) == 0.f);
  m.set("gate", 1.f);
  m.step();
  CHECK(m.gate(1) == 1.f);
  CHECK(m.gate(2) == 0.f);  // the new strum applies to the new note
}

TEST_CASE("chord: state survives a hot-reload copy mid-strum") {
  ChordRig a(2), b(2);
  a.set("strum", 0.01f);
  b.set("strum", 0.01f);
  a.set("gate", 1.f);
  b.set("gate", 1.f);
  a.step(300);
  b.module->copy_state_from(*a.module);
  b.step(180);  // samples 300..479 from the rise
  CHECK(b.gate(2) == 0.f);
  b.step();  // sample 480 from the rise
  CHECK(b.gate(2) == 1.f);
}

TEST_CASE("chord conforms to the voice-bank convention") {
  for (int n : {1, 3, 8}) {
    CAPTURE(n);
    const ModuleInfo* info = find_module("chord");
    REQUIRE(info != nullptr);
    auto module = info->create();
    module->init(kRate, {{"n", number(n)}});
    std::vector<float> ins;
    for (const auto& in : info->inputs) ins.push_back(in.def);
    const int gate = info->input_index("gate");
    const int vel = info->input_index("vel");
    const int strum = info->input_index("strum");
    const int p1 = info->input_index("p1");
    ins[static_cast<size_t>(strum)] = 3.f / 48000.f;  // 3 samples per voice
    // Chords of 6 samples with a one-sample retrigger gap, a rest every
    // fourth, and pitches that wander (slurs) while held.
    auto feed = [&](int step) {
      const int phase = step % 7;
      const bool rest = (step / 7) % 4 == 3;
      ins[static_cast<size_t>(gate)] = (!rest && phase != 6) ? 1.f : 0.f;
      ins[static_cast<size_t>(vel)] = 0.1f * static_cast<float>(step % 10);
      for (int i = 0; i < 8; ++i)
        ins[static_cast<size_t>(p1 + i)] = 0.01f * static_cast<float>(step) + static_cast<float>(i);
    };
    check_bank_conformance(*module, *info, n, ins, feed,
                           /*can_reclaim_while_sounding=*/true, 200,
                           /*min_release_samples=*/0);
  }
}
