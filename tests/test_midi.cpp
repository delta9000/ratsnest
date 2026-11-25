// midi module: pure event -> CV. Everything here is driven by injecting raw
// MIDI bytes through Module::on_midi (no hardware, no transport). See
// docs/superpowers/specs/2026-07-11-midi-design.md §5 for the test list.
#include <cmath>
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
ConfigValue array(std::vector<double> a) {
  ConfigValue v;
  v.arr = std::move(a);
  return v;
}

// Builds a `midi` instance directly and injects events, mirroring AllocRig in
// tests/test_alloc.cpp. midi has no signal inputs, so `ins` is empty.
struct MidiRig {
  const ModuleInfo* info;
  std::unique_ptr<Module> module;
  std::vector<float> ins;
  std::vector<float> outs;

  explicit MidiRig(Config cfg = {}, double sr = 48000.0) {
    info = find_module("midi");
    REQUIRE(info != nullptr);
    module = info->create();
    module->init(sr, cfg);
    ins.assign(info->inputs.size(), 0.f);
    outs.assign(info->outputs.size(), 0.f);
  }

  void ev(uint8_t a, uint8_t b, uint8_t c) {
    const uint8_t data[3] = {a, b, c};
    module->on_midi(data, 3);
  }
  void ev2(uint8_t a, uint8_t b) {
    const uint8_t data[2] = {a, b};
    module->on_midi(data, 2);
  }
  void noteon(int ch, int note, int vel) {
    ev(static_cast<uint8_t>(0x90 | ch), static_cast<uint8_t>(note),
       static_cast<uint8_t>(vel));
  }
  void noteoff(int ch, int note) {
    ev(static_cast<uint8_t>(0x80 | ch), static_cast<uint8_t>(note), 0);
  }
  void cc(int ch, int num, int val) {
    ev(static_cast<uint8_t>(0xB0 | ch), static_cast<uint8_t>(num),
       static_cast<uint8_t>(val));
  }
  void bend(int ch, int value14) {
    ev(static_cast<uint8_t>(0xE0 | ch), static_cast<uint8_t>(value14 & 0x7F),
       static_cast<uint8_t>((value14 >> 7) & 0x7F));
  }
  void pressure(int ch, int val) {
    ev2(static_cast<uint8_t>(0xD0 | ch), static_cast<uint8_t>(val));
  }

  void step() { module->process(ins.data(), outs.data()); }

  float out(const std::string& name) const {
    const int idx = info->output_index(name);
    REQUIRE(idx >= 0);
    return outs[static_cast<size_t>(idx)];
  }
};
}  // namespace

TEST_CASE("midi surface: banked outputs, no mono gate/voct/vel, wants_midi") {
  const ModuleInfo* info = find_module("midi");
  REQUIRE(info != nullptr);
  CHECK(info->wants_midi);
  CHECK(info->inputs.size() == 0);
  // Banked names present; bare mono names deliberately absent (spec §1).
  for (int i = 1; i <= 8; ++i) {
    CHECK(info->output_index("gate" + std::to_string(i)) >= 0);
    CHECK(info->output_index("voct" + std::to_string(i)) >= 0);
    CHECK(info->output_index("vel" + std::to_string(i)) >= 0);
  }
  CHECK(info->output_index("gate") == -1);
  CHECK(info->output_index("voct") == -1);
  CHECK(info->output_index("vel") == -1);
  CHECK(info->output_index("bend") >= 0);
  CHECK(info->output_index("at") >= 0);
  for (int i = 1; i <= 4; ++i)
    CHECK(info->output_index("cc" + std::to_string(i)) >= 0);
}

TEST_CASE("note on/off, voct math, velocity") {
  MidiRig m({{"voices", number(2)}});
  m.noteon(0, 57, 127);
  m.step();
  CHECK(m.out("gate1") == doctest::Approx(1.f));
  CHECK(m.out("voct1") == doctest::Approx(0.f));
  CHECK(m.out("vel1") == doctest::Approx(1.f));
  m.noteon(0, 69, 64);
  m.step();
  CHECK(m.out("gate2") == doctest::Approx(1.f));
  CHECK(m.out("voct2") == doctest::Approx(1.f));  // (69-57)/12
  CHECK(m.out("vel2") == doctest::Approx(64.f / 127.f));
  m.noteoff(0, 57);
  m.step();
  CHECK(m.out("gate1") == doctest::Approx(0.f));
  CHECK(m.out("voct1") == doctest::Approx(0.f));  // holds pitch for tail
}

TEST_CASE("round-robin claims free voices in order") {
  MidiRig m({{"voices", number(3)}});
  m.noteon(0, 60, 100);
  m.step();
  CHECK(m.out("gate1") == doctest::Approx(1.f));
  m.noteon(0, 62, 100);
  m.step();
  CHECK(m.out("gate2") == doctest::Approx(1.f));
  m.noteon(0, 64, 100);
  m.step();
  CHECK(m.out("gate3") == doctest::Approx(1.f));
}

TEST_CASE("oldest steal at capacity inserts one-sample gate gap") {
  MidiRig m({{"voices", number(2)}});
  m.noteon(0, 60, 100);  // voice1
  m.step();
  m.noteon(0, 64, 100);  // voice2
  m.step();
  CHECK(m.out("gate1") == doctest::Approx(1.f));
  CHECK(m.out("gate2") == doctest::Approx(1.f));
  m.noteon(0, 67, 100);  // steal oldest (voice1); gap sample
  m.step();
  CHECK(m.out("gate1") == doctest::Approx(0.f));      // the one-sample gap
  CHECK(m.out("voct1") == doctest::Approx((67 - 57) / 12.f));  // reassigned
  m.step();
  CHECK(m.out("gate1") == doctest::Approx(1.f));      // resumes high after gap
  CHECK(m.out("voct1") == doctest::Approx((67 - 57) / 12.f));
  CHECK(m.out("gate2") == doctest::Approx(1.f));      // untouched
}

TEST_CASE("mono last: newest wins, held key restores on release without retrigger") {
  MidiRig m({{"voices", number(1)}, {"mode", text("last")}});
  m.noteon(0, 57, 100);
  m.step();
  CHECK(m.out("gate1") == doctest::Approx(1.f));
  CHECK(m.out("voct1") == doctest::Approx(0.f));
  m.noteon(0, 60, 100);  // legato to newest, no gap
  m.step();
  CHECK(m.out("gate1") == doctest::Approx(1.f));
  CHECK(m.out("voct1") == doctest::Approx((60 - 57) / 12.f));
  m.noteoff(0, 60);  // restore to 57, gate stays high, no retrigger
  m.step();
  CHECK(m.out("gate1") == doctest::Approx(1.f));
  CHECK(m.out("voct1") == doctest::Approx(0.f));
  m.noteoff(0, 57);
  m.step();
  CHECK(m.out("gate1") == doctest::Approx(0.f));
}

TEST_CASE("mono lowest wins") {
  MidiRig m({{"voices", number(1)}, {"mode", text("lowest")}});
  m.noteon(0, 60, 100);
  m.step();
  CHECK(m.out("voct1") == doctest::Approx((60 - 57) / 12.f));
  m.noteon(0, 57, 100);  // lower key takes over
  m.step();
  CHECK(m.out("voct1") == doctest::Approx(0.f));
  CHECK(m.out("gate1") == doctest::Approx(1.f));
  m.noteoff(0, 57);  // release lowest -> next lowest (60) sounds
  m.step();
  CHECK(m.out("voct1") == doctest::Approx((60 - 57) / 12.f));
  CHECK(m.out("gate1") == doctest::Approx(1.f));
}

TEST_CASE("sustain pedal defers note-off until pedal release") {
  MidiRig m({{"voices", number(2)}});
  m.noteon(0, 60, 100);
  m.step();
  m.cc(0, 64, 127);   // pedal down
  m.noteoff(0, 60);   // deferred
  m.step();
  CHECK(m.out("gate1") == doctest::Approx(1.f));  // still sounding
  m.cc(0, 64, 0);     // pedal up -> deferred off fires
  m.step();
  CHECK(m.out("gate1") == doctest::Approx(0.f));
}

TEST_CASE("sustain pedal is also readable as a mapped cc slot") {
  MidiRig m({{"voices", number(2)}, {"cc", array({64})}});
  m.noteon(0, 60, 100);
  m.cc(0, 64, 127);
  for (int i = 0; i < 2400; ++i) m.step();
  CHECK(m.out("cc1") > 0.99f);                    // pedal level for a soundboard
  m.noteoff(0, 60);
  m.step();
  CHECK(m.out("gate1") == doctest::Approx(1.f));  // and still defers note-off
}

TEST_CASE("poly re-strike of a sustained note reclaims its voice") {
  // Repro (voices:2): noteon 60 -> voice1; pedal down; noteoff 60 -> voice1
  // sustained; noteon 60 again must RECLAIM voice1 (one-sample gap, fresh
  // vel), never claim voice2. Without the fix, the re-strike steals voice2
  // and the later note-off re-marks voice1 (first gate-high match), leaving
  // voice2 gated forever once the pedal lifts.
  MidiRig m({{"voices", number(2)}});
  m.noteon(0, 60, 100);  // claims voice1
  m.step();
  CHECK(m.out("gate1") == doctest::Approx(1.f));
  CHECK(m.out("gate2") == doctest::Approx(0.f));

  m.cc(0, 64, 127);   // pedal down
  m.noteoff(0, 60);   // deferred: voice1 stays sounding (sustained)
  m.step();
  CHECK(m.out("gate1") == doctest::Approx(1.f));
  CHECK(m.out("gate2") == doctest::Approx(0.f));

  m.noteon(0, 60, 110);  // re-strike same note while sustained
  m.step();
  // Reclaim of voice1: one-sample gate gap, voice2 untouched.
  CHECK(m.out("gate1") == doctest::Approx(0.f));
  CHECK(m.out("gate2") == doctest::Approx(0.f));
  m.step();
  // Resumes high on voice1 with the fresh velocity; still only one voice.
  CHECK(m.out("gate1") == doctest::Approx(1.f));
  CHECK(m.out("vel1") == doctest::Approx(110.f / 127.f));
  CHECK(m.out("gate2") == doctest::Approx(0.f));

  m.noteoff(0, 60);   // deferred again (pedal still down)
  m.step();
  CHECK(m.out("gate1") == doctest::Approx(1.f));
  CHECK(m.out("gate2") == doctest::Approx(0.f));

  m.cc(0, 64, 0);     // pedal up: releases the sustained voice
  m.step();
  // No stranded voice: every gate must be low.
  for (int i = 1; i <= 2; ++i)
    CHECK(m.out("gate" + std::to_string(i)) == doctest::Approx(0.f));
}

TEST_CASE("bend folds into voct and its own output") {
  MidiRig m({{"voices", number(1)}, {"bend", number(2)}});
  m.noteon(0, 57, 100);
  m.step();
  CHECK(m.out("voct1") == doctest::Approx(0.f));
  CHECK(m.out("bend") == doctest::Approx(0.f));
  m.bend(0, 16383);  // full up = +2 semitones = 2/12 v/oct
  m.step();
  CHECK(m.out("bend") == doctest::Approx(2.f / 12.f).epsilon(0.001));
  CHECK(m.out("voct1") == doctest::Approx(2.f / 12.f).epsilon(0.001));
  m.bend(0, 0);  // full down = -2 semitones
  m.step();
  CHECK(m.out("bend") == doctest::Approx(-2.f / 12.f).epsilon(0.001));
  CHECK(m.out("voct1") == doctest::Approx(-2.f / 12.f).epsilon(0.001));
  m.bend(0, 8192);  // center
  m.step();
  CHECK(m.out("bend") == doctest::Approx(0.f));
}

TEST_CASE("cc slots map, scale, and smooth with ~3ms one-pole") {
  const double sr = 48000.0;
  MidiRig m({{"voices", number(1)}, {"cc", array({74, 1})}}, sr);
  m.cc(0, 74, 127);  // controller 74 -> cc1, target 1.0
  // Start near zero, ramp toward target; unmapped cc2 (=mod wheel, unset) 0.
  m.step();
  CHECK(m.out("cc1") < 0.1f);       // one-pole has barely moved
  CHECK(m.out("cc2") == doctest::Approx(0.f));  // controller 1 unset
  const int samples_3ms = static_cast<int>(0.003 * sr);
  for (int i = 0; i < samples_3ms; ++i) m.step();
  // After one time constant, within 1/e of the target (reached >= 1-1/e).
  CHECK(m.out("cc1") > 1.f - 1.f / std::exp(1.f) - 0.05f);
  CHECK(m.out("cc1") < 1.f);
  // cc3/cc4 unmapped -> silent.
  CHECK(m.out("cc3") == doctest::Approx(0.f));
  CHECK(m.out("cc4") == doctest::Approx(0.f));
  // Map check: controller 1 drives cc2.
  m.cc(0, 1, 127);
  for (int i = 0; i < samples_3ms * 6; ++i) m.step();
  CHECK(m.out("cc2") > 0.9f);
}

TEST_CASE("channel filter: specific channel ignores others, omni takes all") {
  MidiRig specific({{"voices", number(1)}, {"channel", number(2)}});
  specific.noteon(0, 60, 100);  // channel 1 (0-based 0): ignored
  specific.step();
  CHECK(specific.out("gate1") == doctest::Approx(0.f));
  specific.noteon(1, 60, 100);  // channel 2 (0-based 1): accepted
  specific.step();
  CHECK(specific.out("gate1") == doctest::Approx(1.f));

  MidiRig omni({{"voices", number(1)}, {"channel", number(0)}});
  omni.noteon(5, 64, 100);  // any channel accepted
  omni.step();
  CHECK(omni.out("gate1") == doctest::Approx(1.f));
}

TEST_CASE("velocity 0 note-on is a note-off") {
  MidiRig m({{"voices", number(1)}});
  m.noteon(0, 60, 100);
  m.step();
  CHECK(m.out("gate1") == doctest::Approx(1.f));
  m.noteon(0, 60, 0);  // running-status note-off
  m.step();
  CHECK(m.out("gate1") == doctest::Approx(0.f));
}

TEST_CASE("channel pressure drives at output") {
  MidiRig m({{"voices", number(1)}});
  m.pressure(0, 127);
  m.step();
  CHECK(m.out("at") == doctest::Approx(1.f));
  m.pressure(0, 64);
  m.step();
  CHECK(m.out("at") == doctest::Approx(64.f / 127.f));
}

TEST_CASE("config values clamp in init (no range errors)") {
  // channel 50 clamps to 16; sent events use that same channel so the filter
  // (not a range error) is what's under test elsewhere. Here events are omni.
  MidiRig m({{"voices", number(99)}, {"channel", number(0)},
             {"bend", number(100)}, {"cc", array({999, -3, 74, 1, 5})}});
  // voices clamps to 8: gate8 exists and can sound.
  for (int i = 0; i < 8; ++i) m.noteon(0, 60 + i, 100), m.step();
  CHECK(m.out("gate8") == doctest::Approx(1.f));
  // Only first four cc entries read (clamped 0..127); 999 -> 127, -3 -> 0.
  m.cc(0, 127, 127);
  for (int i = 0; i < 400; ++i) m.step();
  CHECK(m.out("cc1") > 0.9f);
}

TEST_CASE("midi state migrates mid-held-chord") {
  const ModuleInfo* info = find_module("midi");
  REQUIRE(info != nullptr);
  Config cfg = {{"voices", number(3)}, {"cc", array({74})}};
  auto a = info->create();
  auto b = info->create();
  a->init(48000.0, cfg);
  b->init(48000.0, cfg);
  std::vector<float> outs_a(info->outputs.size(), 0.f);
  std::vector<float> outs_b(info->outputs.size(), 0.f);
  auto inject = [](Module* m, uint8_t s, uint8_t d1, uint8_t d2) {
    const uint8_t data[3] = {s, d1, d2};
    m->on_midi(data, 3);
  };
  // Hold a three-note chord, add bend and cc, on instance a.
  inject(a.get(), 0x90, 57, 100);
  inject(a.get(), 0x90, 60, 90);
  inject(a.get(), 0x90, 64, 80);
  inject(a.get(), 0xE0, 0x00, 0x60);  // some bend
  inject(a.get(), 0xB0, 74, 100);     // cc1
  for (int i = 0; i < 200; ++i) a->process(std::vector<float>().data(), outs_a.data());

  b->copy_state_from(*a);
  // After migration, both produce identical output for further steps,
  // including note-offs that must find the migrated voices.
  auto both = [&](uint8_t s, uint8_t d1, uint8_t d2) {
    inject(a.get(), s, d1, d2);
    inject(b.get(), s, d1, d2);
  };
  both(0x80, 60, 0);  // release middle note
  for (int i = 0; i < 50; ++i) {
    a->process(std::vector<float>().data(), outs_a.data());
    b->process(std::vector<float>().data(), outs_b.data());
    for (size_t j = 0; j < outs_a.size(); ++j)
      CHECK(outs_a[j] == doctest::Approx(outs_b[j]));
  }
}

namespace {
// Conformance feed: injects notes so that more keys are held than there are
// voices (voices:2, up to 4 simultaneous presses) -> genuine oldest-steal
// while sounding, producing REAL one-sample reclaim gaps. Genuine releases
// leave voices low for many samples (>= min_release_samples). This is the
// coupling payoff the bank suite exists to check (see bank_conformance.h).
struct MidiConformanceFeed {
  MidiRig& rig;
  int cycle = 0;
  void operator()(int step) {
    const int period = 12;
    const int phase = step % period;
    if (phase == 0) ++cycle;
    // Distinct notes per cycle so voct/vel vary across claims.
    const int base = 48 + (cycle % 3) * 5;
    switch (phase) {
      case 0: rig.noteon(0, base + 0, 100); break;  // claim free voice
      case 1: rig.noteon(0, base + 4, 110); break;  // claim other free voice
      case 2: rig.noteon(0, base + 7, 90); break;   // steal oldest (gap)
      case 3: rig.noteon(0, base + 11, 80); break;  // steal oldest (gap)
      case 4: rig.noteoff(0, base + 7); break;       // genuine release
      case 5: rig.noteoff(0, base + 11); break;      // genuine release
      default: break;                                // idle: long low run
    }
  }
};
}  // namespace

TEST_CASE("midi passes bank conformance (reclaim-while-sounding)") {
  MidiRig rig({{"voices", number(2)}});
  MidiConformanceFeed feed{rig};
  // can_reclaim_while_sounding = true: midi genuinely steals sounding voices,
  // so the exact-one-sample reclaim-gap property is actively asserted. Steal
  // gaps are 1 sample; genuine releases stay low >= 3 samples.
  check_bank_conformance(*rig.module, *rig.info, 2, rig.ins, feed,
                         /*can_reclaim_while_sounding=*/true, /*steps=*/64,
                         /*min_release_samples=*/3);
}

TEST_CASE("note-off and note-on on the same frame still retrigger") {
  // A sequencer's repeated note lands its note-off and the next note-on on
  // one frame. The gate never had a low sample between them, so without a
  // gap the envelope downstream would hear one long note.
  SUBCASE("mono") {
    MidiRig m({{"voices", number(1)}});
    m.noteon(0, 60, 100);
    m.step();
    m.noteoff(0, 60);
    m.noteon(0, 60, 90);
    m.step();
    CHECK(m.out("gate1") == doctest::Approx(0.f));  // the retrigger gap
    m.step();
    CHECK(m.out("gate1") == doctest::Approx(1.f));
    CHECK(m.out("vel1") == doctest::Approx(90.f / 127.f));
    m.noteon(0, 64, 100);  // legato onto a held key stays gapless
    m.step();
    CHECK(m.out("gate1") == doctest::Approx(1.f));
  }
  SUBCASE("poly, reclaiming the only free voice") {
    MidiRig m({{"voices", number(2)}});
    m.noteon(0, 48, 100);  // voice1 holds
    m.noteon(0, 60, 100);  // voice2
    m.step();
    m.noteoff(0, 60);
    m.noteon(0, 62, 100);  // voice2 is the only free one
    m.step();
    CHECK(m.out("gate1") == doctest::Approx(1.f));
    CHECK(m.out("gate2") == doctest::Approx(0.f));
    m.step();
    CHECK(m.out("gate2") == doctest::Approx(1.f));
    CHECK(m.out("voct2") == doctest::Approx((62 - 57) / 12.f));
  }
}

TEST_CASE("all notes off and all sound off release every voice") {
  for (int voices : {1, 3}) {
    CAPTURE(voices);
    MidiRig m({{"voices", number(voices)}});
    const auto high = [&] {
      int n = 0;
      for (int i = 1; i <= 3; ++i) n += m.out("gate" + std::to_string(i)) > 0.5f;
      return n;
    };
    m.noteon(0, 60, 100);
    m.noteon(0, 64, 100);
    m.step();
    m.cc(0, 123, 0);  // All Notes Off
    m.step();
    CHECK(high() == 0);

    // Under the pedal, All Notes Off sustains; All Sound Off does not.
    m.noteon(0, 60, 100);
    m.cc(0, 64, 127);
    m.cc(0, 123, 0);
    m.step();
    CHECK(high() == 1);
    m.cc(0, 120, 0);
    m.step();
    CHECK(high() == 0);
    m.cc(0, 64, 0);  // nothing left stranded
    m.noteon(0, 67, 100);
    m.step();
    CHECK(high() == 1);
  }
}
