// The tracker module: timing against the clock, gates, the order list,
// reset, hot-reload state, and the file loader's diagnostics and watching.
#include <array>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>
#include <doctest/doctest.h>
#include "dsp/modules.h"
#include "engine/controller.h"
#include "engine/rt_runner.h"
#include "graph/subs.h"

using namespace ms;

namespace {
enum { kVoct, kGate, kVel, kCva, kCvb };
constexpr int kStep = 40, kPattern = 41, kRow = 42, kStepTrig = 43, kRowTrig = 44, kEnd = 45;

int col(int track, int c) { return track * 5 + c; }

struct Trk {
  std::unique_ptr<dsp::Module> m;
  std::array<float, 3> in{};  // clock, reset, start
  std::array<float, 46> out{};
  explicit Trk(const std::string& text) {
    const dsp::ModuleInfo* info = dsp::find_module("tracker");
    REQUIRE(info != nullptr);
    REQUIRE(info->outputs.size() == out.size());
    m = info->create();
    dsp::ConfigValue v;
    v.str = text;
    m->init(48000.0, {{"text", v}});
  }
  float run(float clock) {
    in[0] = clock;
    m->process(in.data(), out.data());
    return out[static_cast<size_t>(col(0, kGate))];
  }
  // One clock period: the edge sample, then `low` samples low. Returns the
  // outputs seen on the edge sample.
  std::array<float, 46> tick(int high = 2, int low = 2) {
    run(1.0f);
    const auto edge = out;
    for (int i = 1; i < high; ++i) run(1.0f);
    for (int i = 0; i < low; ++i) run(0.0f);
    return edge;
  }
  float o(int i) const { return out[static_cast<size_t>(i)]; }
};

float at(const std::array<float, 46>& o, int i) { return o[static_cast<size_t>(i)]; }

const char* kScale =
    "track a note vel cva\n"
    "pattern p 4\n| C4 .5 1\n| D4 . 2\n| D4\n| ~E4\norder p\n";
}  // namespace

TEST_CASE("tracker registers with five banks of eight and six song outputs") {
  const dsp::ModuleInfo* info = dsp::find_module("tracker");
  REQUIRE(info);
  CHECK(info->output_index("voct1") == 0);
  CHECK(info->output_index("gate1") == 1);
  CHECK(info->output_index("cvb8") == 39);
  CHECK(info->output_index("end") == kEnd);
  CHECK(info->config("file") != nullptr);
  CHECK(info->config("text") != nullptr);
}

TEST_CASE("tracker: row 0 plays on the first edge; one row per rising edge") {
  Trk t(kScale);
  t.run(0.0f);
  CHECK(t.o(col(0, kGate)) == 0.0f);                       // waits for a clock
  CHECK(t.o(col(0, kVoct)) == doctest::Approx(0.25f));     // shows row 0's values
  auto e = t.tick();
  CHECK(at(e, col(0, kGate)) == 1.0f);                     // on the edge sample
  CHECK(at(e, kRowTrig) == 1.0f);
  CHECK(at(e, kStepTrig) == 1.0f);
  CHECK(at(e, col(0, kVel)) == doctest::Approx(0.5f));
  CHECK(at(e, col(0, kCva)) == doctest::Approx(1.0f));
  CHECK(t.o(kRowTrig) == 0.0f);                            // one-sample pulse
  CHECK(t.o(col(0, kGate)) == 1.0f);                       // held through the low half
  e = t.tick();
  CHECK(at(e, kRow) == 1.0f);
  CHECK(at(e, kStepTrig) == 0.0f);
  CHECK(at(e, col(0, kVoct)) == doctest::Approx(5.0f / 12));
  CHECK(at(e, col(0, kVel)) == doctest::Approx(0.5f));      // held
  CHECK(at(e, col(0, kCva)) == doctest::Approx(2.0f));
  // A clock held high plays one row, not one per sample.
  for (int i = 0; i < 100; ++i) t.run(1.0f);
  CHECK(t.o(kRow) == 2.0f);
}

TEST_CASE("tracker: a repeated note re-articulates with a one-sample gap; a slur does not") {
  Trk t(kScale);
  t.tick();
  t.tick();
  auto e = t.tick();                       // row 2: D4 again
  CHECK(at(e, col(0, kGate)) == 0.0f);     // the gap is the edge sample
  CHECK(t.o(col(0, kGate)) == 1.0f);       // and only that sample
  e = t.tick();                            // row 3: ~E4
  CHECK(at(e, col(0, kGate)) == 1.0f);
  CHECK(at(e, col(0, kVoct)) == doctest::Approx(7.0f / 12));
}

TEST_CASE("tracker: off drops the gate on its edge; a note from silence rises on its edge") {
  Trk t("track a note\npattern p 3\n| C4\n| off\n| C4\norder p\n");
  t.tick();
  auto e = t.tick();
  CHECK(at(e, col(0, kGate)) == 0.0f);
  CHECK(t.o(col(0, kGate)) == 0.0f);
  e = t.tick();
  CHECK(at(e, col(0, kGate)) == 1.0f);
}

TEST_CASE("tracker: gates do not depend on the clock's duty cycle") {
  Trk t("track a note\npattern p 2\n| x\n| x\norder p\n");
  t.tick(1, 50);  // a one-sample clock pulse
  CHECK(t.o(col(0, kGate)) == 1.0f);
}

TEST_CASE("tracker: undeclared tracks output zeros") {
  Trk t(kScale);
  t.tick();
  for (int c = 0; c < 5; ++c) CHECK(t.o(col(1, c)) == 0.0f);
  CHECK(t.o(col(7, kVel)) == 0.0f);
}

TEST_CASE("tracker: order steps report step, pattern and step_trig") {
  Trk t("track a cva\npattern x 2\n| 1\npattern y 1\n| 2\norder y x*2\n");
  std::vector<float> steps, pats, rows, trigs;
  for (int i = 0; i < 5; ++i) {
    auto e = t.tick();
    steps.push_back(at(e, kStep));
    pats.push_back(at(e, kPattern));
    rows.push_back(at(e, kRow));
    trigs.push_back(at(e, kStepTrig));
  }
  CHECK(steps == std::vector<float>{0, 1, 1, 2, 2});
  CHECK(pats == std::vector<float>{1, 0, 0, 0, 0});
  CHECK(rows == std::vector<float>{0, 0, 1, 0, 1});
  CHECK(trigs == std::vector<float>{1, 1, 0, 1, 0});
}

TEST_CASE("tracker: without a loop point the song ends and stays ended") {
  Trk t("track a note cva\npattern p 2\n| C4 5\n| . 6\norder p\n");
  t.tick();
  t.tick();
  auto e = t.tick();
  CHECK(at(e, kEnd) == 1.0f);
  CHECK(at(e, col(0, kGate)) == 0.0f);
  CHECK(at(e, kRowTrig) == 0.0f);
  CHECK(at(e, col(0, kCva)) == doctest::Approx(6.0f));  // values hold
  for (int i = 0; i < 3; ++i) t.tick();
  CHECK(t.o(kEnd) == 1.0f);
  CHECK(t.o(kRow) == 1.0f);
}

TEST_CASE("tracker: a loop point wraps, pulsing end for one sample") {
  Trk t("track a cva\npattern i 1\n| 1\npattern l 2\n| 2\n| 3\norder i |: l\n");
  t.tick();
  t.tick();
  t.tick();
  auto e = t.tick();  // past the end: back to step 1
  CHECK(at(e, kEnd) == 1.0f);
  CHECK(at(e, kStep) == 1.0f);
  CHECK(at(e, kStepTrig) == 1.0f);
  CHECK(at(e, col(0, kCva)) == doctest::Approx(2.0f));
  CHECK(t.o(kEnd) == 0.0f);
}

TEST_CASE("tracker: reset rewinds to step `start`; a coincident clock plays its row 0") {
  Trk t("track a cva\npattern x 2\n| 1\n| 2\npattern y 2\n| 3\n| 4\norder x y\n");
  t.tick();
  t.tick();
  t.tick();
  t.in[1] = 1.0f;  // reset with the clock low: wait for the next edge
  t.run(0.0f);
  CHECK(t.o(col(0, kGate)) == 0.0f);
  CHECK(t.o(kRow) == 0.0f);
  t.in[1] = 0.0f;
  auto e = t.tick();
  CHECK(at(e, col(0, kCva)) == 1.0f);
  CHECK(at(e, kStepTrig) == 1.0f);
  t.in[2] = 1.0f;  // jump to step 1, reset and clock on one sample
  t.in[1] = 1.0f;
  e = t.tick();
  CHECK(at(e, kStep) == 1.0f);
  CHECK(at(e, col(0, kCva)) == 3.0f);
  t.in[1] = 0.0f;
  t.in[2] = 99.0f;  // clamped when used
  t.in[1] = 1.0f;
  e = t.tick();
  CHECK(at(e, kStep) == 1.0f);
}

TEST_CASE("tracker: hot reload keeps the order step and the row within it") {
  const std::string a = "track a cva\npattern p 4\n| 0\n| 1\n| 2\n| 3\norder p p\n";
  Trk old(a);
  for (int i = 0; i < 6; ++i) old.tick();  // step 1, row 1
  Trk edited("track a cva\npattern p 4\n| 10\n| 11\n| 12\n| 13\norder p p\n");
  edited.m->copy_state_from(*old.m);
  edited.run(0.0f);
  CHECK(edited.o(kStep) == 1.0f);
  CHECK(edited.o(kRow) == 1.0f);
  CHECK(edited.o(col(0, kCva)) == 11.0f);
  CHECK(edited.o(col(0, kGate)) == old.o(col(0, kGate)));
  CHECK(at(edited.tick(), col(0, kCva)) == 12.0f);

  Trk shorter("track a cva\npattern p 1\n| 7\norder p\n");  // step 1 is gone
  shorter.m->copy_state_from(*old.m);
  shorter.run(0.0f);
  CHECK(shorter.o(kStep) == 0.0f);
  CHECK(shorter.o(kRow) == 0.0f);
  CHECK(at(shorter.tick(), kEnd) == 1.0f);  // was past its end: it ends

  Trk fresh(a);  // not started yet: still waits for the first edge
  Trk copy(a);
  copy.m->copy_state_from(*fresh.m);
  CHECK(at(copy.tick(), kRow) == 0.0f);
}

TEST_CASE("tracker: an empty or broken song outputs zeros") {
  for (const char* text : {"", "garbage", "track a note\npattern p 1\n"}) {
    Trk t(text);
    t.tick();
    for (float v : t.out) CHECK(v == 0.0f);
    Trk u(kScale);
    u.m->copy_state_from(*t.m);  // migrating from an empty song is safe
    CHECK(at(u.tick(), kRow) == 0.0f);
  }
}

// ---- loader -----------------------------------------------------------------

namespace {
struct TempDir {
  std::filesystem::path dir;
  TempDir() {
    dir = std::filesystem::temp_directory_path() / "ms_tracker_test";
    std::filesystem::remove_all(dir);
    std::filesystem::create_directories(dir / "songs");
  }
  ~TempDir() { std::filesystem::remove_all(dir); }
  std::string write(const std::string& name, const std::string& text) const {
    std::ofstream(dir / name) << text;
    return (dir / name).string();
  }
};
const char* kPatch =
    "clk = clock { bpm: 6000 }\n"        // one row per 480 samples
    "song = tracker { file: \"songs/a.trk\" }\n"
    "clk -> song.clock\n"
    "song.cva1 -> out.left\n";
std::string ramp(float base) {
  std::string s = "track a cva\npattern p 64\n";
  for (int r = 0; r < 64; ++r) s += "| " + std::to_string(base + 0.01f * static_cast<float>(r)) + "\n";
  return s + "order p\n";
}
}  // namespace

TEST_CASE("tracker loader: resolves against the patch, embeds the song, lists the file") {
  TempDir d;
  const std::string trk = d.write("songs/a.trk", ramp(0.0f));
  const std::string patch = d.write("p.patch", kPatch);
  auto b = graph::build_patch(kPatch, patch, 48000.0);
  REQUIRE(b.compiled.ok());
  REQUIRE(b.files.size() == 1);
  CHECK(std::filesystem::equivalent(b.files[0], trk));
}

TEST_CASE("tracker loader: .trk errors carry the .trk's file:line:col") {
  TempDir d;
  d.write("songs/a.trk", "track a cva\npattern p 4\n| 1\n| oops\norder p\n");
  const std::string patch = d.write("p.patch", kPatch);
  auto b = graph::build_patch(kPatch, patch, 48000.0);
  REQUIRE(b.compiled.errors.size() == 1);
  const Diag& e = b.compiled.errors[0];
  CHECK(e.file.find("a.trk") != std::string::npos);
  CHECK(e.line == 4);
  CHECK(e.col == 3);
  CHECK(b.files.size() == 1);  // still watched, so fixing it reloads

  std::filesystem::remove(d.dir / "songs/a.trk");
  auto missing = graph::build_patch(kPatch, patch, 48000.0);
  REQUIRE(missing.compiled.errors.size() == 1);
  CHECK(missing.compiled.errors[0].line == 2);
  CHECK(missing.compiled.errors[0].msg.find("cannot read tracker file") != std::string::npos);
}

TEST_CASE("tracker loader: a .trk edit reloads the topology and keeps position") {
  TempDir d;
  d.write("songs/a.trk", ramp(0.0f));
  const std::string patch = d.write("p.patch", kPatch);
  engine::RtRunner run(48000.0);
  engine::Controller ctl(run, 48000.0);
  std::vector<float> l(4800), r(4800);
  REQUIRE(ctl.load(kPatch, patch).ok);
  run.render(l.data(), r.data(), 4800);  // rows 0..9
  CHECK(l[4799] == doctest::Approx(0.09f));

  d.write("songs/a.trk", ramp(0.5f));
  auto res = ctl.load(kPatch, patch);
  REQUIRE(res.ok);
  CHECK(res.topology);
  run.render(l.data(), r.data(), 4800);  // through the crossfade: rows 10..19
  CHECK(l[4799] == doctest::Approx(0.5f + 0.19f));

  d.write("songs/a.trk", "track a cva\npattern p 4\n| nope\norder p\n");
  CHECK_FALSE(ctl.load(kPatch, patch).ok);  // the last good graph keeps playing
  run.render(l.data(), r.data(), 480);
  CHECK(l[479] == doctest::Approx(0.5f + 0.20f));
}
