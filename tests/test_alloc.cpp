#include <string>
#include <vector>
#include <doctest/doctest.h>
#include "dsp/modules.h"
#include "bank_conformance.h"

using namespace ms::dsp;
using ms::dsp::test::check_bank_conformance;

namespace {
ConfigValue number(double n) {
  ConfigValue v;
  v.num = n;
  v.is_num = true;
  return v;
}
ConfigValue text(const char* s) {
  ConfigValue v;
  v.str = s;
  return v;
}

// Drives an `alloc` instance by name (gate/voct/vel in, named banks out),
// mirroring the port-bank idiom already exercised for `turing.bit1..8`.
struct AllocRig {
  const ModuleInfo* info;
  std::unique_ptr<Module> module;
  std::vector<float> ins;
  std::vector<float> outs;

  explicit AllocRig(int n, const char* mode = "rr") {
    info = find_module("alloc");
    REQUIRE(info != nullptr);
    module = info->create();
    Config cfg = {{"n", number(n)}, {"mode", text(mode)}};
    module->init(48000.0, cfg);
    ins.assign(info->inputs.size(), 0.f);
    outs.assign(info->outputs.size(), 0.f);
  }

  void step(float gate, float voct, float vel) {
    ins[0] = gate;
    ins[1] = voct;
    ins[2] = vel;
    module->process(ins.data(), outs.data());
  }

  float out(const std::string& name) const {
    const int idx = info->output_index(name);
    REQUIRE(idx >= 0);
    return outs[static_cast<size_t>(idx)];
  }
};
}  // namespace

TEST_CASE("alloc registers gate/voct/vel inputs and 64 banked outputs in order") {
  const ModuleInfo* info = find_module("alloc");
  REQUIRE(info != nullptr);
  REQUIRE(info->inputs.size() == 3);
  CHECK(std::string(info->inputs[0].name) == "gate");
  CHECK(std::string(info->inputs[1].name) == "voct");
  CHECK(std::string(info->inputs[2].name) == "vel");
  REQUIRE(info->outputs.size() == 64);
  const char* stems[4] = {"gate", "voct", "vel", "spread"};
  for (int f = 0; f < 4; ++f)
    for (int i = 0; i < 16; ++i)
      CHECK(std::string(info->outputs[static_cast<size_t>(16 * f + i)].name) ==
            std::string(stems[f]) + std::to_string(i + 1));
}

TEST_CASE("rr claims voices in order and older voices hold pitch") {
  AllocRig m(3);
  m.step(1, 0.25f, 1);
  CHECK(m.out("gate1") == doctest::Approx(1.0f));
  CHECK(m.out("voct1") == doctest::Approx(0.25f));
  m.step(1, 0.30f, 1);
  CHECK(m.out("voct1") == doctest::Approx(0.30f));  // tracks while newest
  m.step(0, 0.30f, 1);
  CHECK(m.out("gate1") == doctest::Approx(0.0f));
  m.step(1, 0.90f, 1);
  CHECK(m.out("gate2") == doctest::Approx(1.0f));  // next voice claimed
  CHECK(m.out("voct1") == doctest::Approx(0.30f));  // held for release tail
}

TEST_CASE("supersede drops gate the same sample a new voice is claimed") {
  AllocRig m(3);
  m.step(1, 0.1f, 1);  // claim voice1
  CHECK(m.out("gate1") == doctest::Approx(1.0f));
  m.step(0, 0.1f, 1);  // release voice1: gate drops immediately
  CHECK(m.out("gate1") == doctest::Approx(0.0f));
  m.step(1, 0.5f, 1);  // claim voice2 on the same edge voice1 stays superseded
  CHECK(m.out("gate1") == doctest::Approx(0.0f));
  CHECK(m.out("gate2") == doctest::Approx(1.0f));
  CHECK(m.out("voct1") == doctest::Approx(0.1f));  // held, not reset to 0
}

TEST_CASE("wrapped reclaim re-sounds the voice with the new pitch on the claim sample") {
  // Note: this is NOT a test of the gap_ mechanism. With a single mono gate
  // input, a rising edge implies the previous sample was <=0.5, which already
  // dropped every voice's gate — so gap_[i] is always false at the moment a
  // voice is reclaimed and the defensive one-sample gap can never fire here.
  // It stays in alloc.cpp because it defines the bank-producer convention a
  // future producer with real note identity (midi) must honor when it CAN
  // reclaim a voice while its gate is still high; midi's own tests exercise
  // that path. Here we only assert the ordinary, reachable behavior: the
  // reclaimed voice's gate is high and its voct is the new pitch on the very
  // sample it is claimed.
  AllocRig m(3);
  auto claim_and_release = [&](float voct) {
    m.step(1, voct, 1);
    m.step(0, voct, 1);
  };
  claim_and_release(0.1f);  // voice1
  claim_and_release(0.2f);  // voice2
  claim_and_release(0.3f);  // voice3
  // fourth edge wraps back to voice1
  m.step(1, 0.4f, 1);
  CHECK(m.out("gate1") == doctest::Approx(1.0f));
  CHECK(m.out("voct1") == doctest::Approx(0.4f));
}

TEST_CASE("vel latches at claim and holds until the voice's next claim") {
  AllocRig m(2);
  m.step(1, 0.1f, 0.3f);  // claim voice1 (newest=0) at vel 0.3
  CHECK(m.out("vel1") == doctest::Approx(0.3f));
  m.step(1, 0.1f, 0.9f);  // gate still high, vel input changes: no re-claim
  CHECK(m.out("vel1") == doctest::Approx(0.3f));  // latched, not tracking
  m.step(0, 0.1f, 0.9f);  // release: vel still holds
  CHECK(m.out("vel1") == doctest::Approx(0.3f));
  m.step(1, 0.5f, 0.9f);  // claim voice2 (newest=1), voice1 untouched
  CHECK(m.out("vel1") == doctest::Approx(0.3f));
  CHECK(m.out("vel2") == doctest::Approx(0.9f));
  m.step(0, 0.5f, 0.9f);  // release voice2
  m.step(1, 0.9f, 0.6f);  // rising edge wraps (newest=0): reclaims voice1
  CHECK(m.out("vel1") == doctest::Approx(0.6f));  // re-latched on new claim
}

TEST_CASE("n=1 rr is legato: gate held high tracks voct continuously with no retrigger") {
  AllocRig m(1);
  m.step(1, 0.2f, 1);
  CHECK(m.out("gate1") == doctest::Approx(1.0f));
  CHECK(m.out("voct1") == doctest::Approx(0.2f));
  m.step(1, 0.4f, 1);  // still high: same claim, voct tracks, no retrigger
  CHECK(m.out("gate1") == doctest::Approx(1.0f));
  CHECK(m.out("voct1") == doctest::Approx(0.4f));
  m.step(1, 0.7f, 1);
  CHECK(m.out("gate1") == doctest::Approx(1.0f));
  CHECK(m.out("voct1") == doctest::Approx(0.7f));
  m.step(0, 0.7f, 1);  // gate falls: voct holds at its last tracked value
  CHECK(m.out("gate1") == doctest::Approx(0.0f));
  CHECK(m.out("voct1") == doctest::Approx(0.7f));
}

TEST_CASE("unison: all gates follow, spread is centered") {
  AllocRig m(4, "unison");
  m.step(1, 0.5f, 0.8f);
  for (const char* g : {"gate1", "gate2", "gate3", "gate4"})
    CHECK(m.out(g) == doctest::Approx(1.0f));
  for (const char* v : {"voct1", "voct2", "voct3", "voct4"})
    CHECK(m.out(v) == doctest::Approx(0.5f));
  CHECK(m.out("spread1") == doctest::Approx(-1.0f));
  CHECK(m.out("spread2") == doctest::Approx(-1.0f / 3.0f));
  CHECK(m.out("spread3") == doctest::Approx(1.0f / 3.0f));
  CHECK(m.out("spread4") == doctest::Approx(1.0f));

  m.step(0, 0.5f, 0.8f);
  for (const char* g : {"gate1", "gate2", "gate3", "gate4"})
    CHECK(m.out(g) == doctest::Approx(0.0f));  // all follow input down too
}

TEST_CASE("unison with a single voice has zero spread") {
  AllocRig m(1, "unison");
  m.step(1, 0.5f, 1);
  CHECK(m.out("spread1") == doctest::Approx(0.0f));
}

TEST_CASE("banks above n stay zero") {
  AllocRig m(2);
  m.step(1, 0.6f, 0.9f);
  m.step(1, 0.6f, 0.9f);
  m.step(0, 0.6f, 0.9f);
  m.step(1, 0.7f, 0.9f);
  for (int i = 3; i <= 8; ++i) {
    CHECK(m.out("gate" + std::to_string(i)) == doctest::Approx(0.0f));
    CHECK(m.out("voct" + std::to_string(i)) == doctest::Approx(0.0f));
    CHECK(m.out("vel" + std::to_string(i)) == doctest::Approx(0.0f));
    CHECK(m.out("spread" + std::to_string(i)) == doctest::Approx(0.0f));
  }
}

TEST_CASE("alloc migrates claim state") {
  const ModuleInfo* info = find_module("alloc");
  REQUIRE(info != nullptr);
  Config cfg = {{"n", number(3)}};
  auto a = info->create();
  auto b = info->create();
  a->init(48000.0, cfg);
  b->init(48000.0, cfg);
  float ins[3] = {0.0f, 0.0f, 1.0f};
  float out_a[64] = {}, out_b[64] = {};
  auto step = [&](Module* m, float* out, float gate, float voct, float vel) {
    ins[0] = gate;
    ins[1] = voct;
    ins[2] = vel;
    m->process(ins, out);
  };
  step(a.get(), out_a, 1, 0.1f, 0.5f);  // claim voice1
  step(a.get(), out_a, 0, 0.1f, 0.5f);  // release
  step(a.get(), out_a, 1, 0.6f, 0.9f);  // claim voice2, mid-claim
  b->copy_state_from(*a);
  for (int i = 0; i < 8; ++i) {
    const float gate = (i % 2 == 0) ? 1.0f : 0.0f;
    const float voct = 0.6f + static_cast<float>(i) * 0.01f;
    ins[0] = gate;
    ins[1] = voct;
    ins[2] = 0.9f;
    a->process(ins, out_a);
    b->process(ins, out_b);
    for (int j = 0; j < 64; ++j)
      CHECK(out_a[j] == doctest::Approx(out_b[j]));
  }
}

namespace {
// Drives an AllocRig through repeating claim/release cycles: 5 samples
// high, 3 samples low, forever. Each rising edge bumps `edge` (used to vary
// voct/vel per claim so the hold rule and per-bank tracking are both
// exercised) — shared shape for all three bank-conformance instantiations
// below; only `n`/`mode` differ per rig.
struct CyclingFeed {
  AllocRig& rig;
  int edge = 0;

  void operator()(int step) {
    const int period = 8;
    const int phase = step % period;
    const bool gate_on = phase < 5;
    if (gate_on && phase == 0) ++edge;
    rig.ins[0] = gate_on ? 1.0f : 0.0f;
    rig.ins[1] = 0.1f + 0.05f * static_cast<float>(edge);
    rig.ins[2] = (edge % 2 == 0) ? 0.4f : 0.8f;
  }
};
}  // namespace

TEST_CASE("bank conformance: rr n=3") {
  AllocRig rig(3, "rr");
  CyclingFeed feed{rig};
  // alloc is driven by a single mono `gate` input: a claim can only occur
  // on a rising edge, which by construction already requires the prior
  // sample to be low, so the reclaim-gap property can never be exercised
  // through this seam — see bank_conformance.h property 4.
  check_bank_conformance(*rig.module, *rig.info, 3, rig.ins,
                          feed, /*can_reclaim_while_sounding=*/false);
}

TEST_CASE("bank conformance: unison n=4") {
  AllocRig rig(4, "unison");
  CyclingFeed feed{rig};
  check_bank_conformance(*rig.module, *rig.info, 4, rig.ins,
                          feed, /*can_reclaim_while_sounding=*/false);
}

TEST_CASE("bank conformance: rr n=1") {
  AllocRig rig(1, "rr");
  CyclingFeed feed{rig};
  check_bank_conformance(*rig.module, *rig.info, 1, rig.ins,
                          feed, /*can_reclaim_while_sounding=*/false);
}
