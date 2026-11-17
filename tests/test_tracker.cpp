// The tracker module: timing against the clock, gates, the order list,
// reset, hot-reload state, and the file loader's diagnostics and watching.
#include <algorithm>
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
enum { kVoct, kGate, kVel, kCva, kCvb, kCvd, kCols };
constexpr int kStep = 16 * kCols, kPattern = kStep + 1, kRow = kStep + 2, kStepTrig = kStep + 3,
              kRowTrig = kStep + 4, kEnd = kStep + 5, kOuts = kStep + 6;

int col(int track, int c) { return track * kCols + c; }

struct Trk {
  std::unique_ptr<dsp::Module> m;
  std::array<float, 3> in{};  // clock, reset, start
  std::array<float, kOuts> out{};
  explicit Trk(const std::string& text, double seed = 1.0) {
    const dsp::ModuleInfo* info = dsp::find_module("tracker");
    REQUIRE(info != nullptr);
    REQUIRE(info->outputs.size() == out.size());
    m = info->create();
    dsp::ConfigValue v, sd;
    v.str = text;
    sd.num = seed;
    sd.is_num = true;
    m->init(48000.0, {{"text", v}, {"seed", sd}});
  }
  // Output `i` over one 100-sample clock period, edge first.
  std::vector<float> period(int i) {
    std::vector<float> v;
    for (int s = 0; s < 100; ++s) {
      run(s < 50 ? 1.0f : 0.0f);
      v.push_back(o(i));
    }
    return v;
  }
  float run(float clock) {
    in[0] = clock;
    m->process(in.data(), out.data());
    return out[static_cast<size_t>(col(0, kGate))];
  }
  // One clock period: the edge sample, then `low` samples low. Returns the
  // outputs seen on the edge sample.
  std::array<float, kOuts> tick(int high = 2, int low = 2) {
    run(1.0f);
    const auto edge = out;
    for (int i = 1; i < high; ++i) run(1.0f);
    for (int i = 0; i < low; ++i) run(0.0f);
    return edge;
  }
  float o(int i) const { return out[static_cast<size_t>(i)]; }
};

float at(const std::array<float, kOuts>& o, int i) { return o[static_cast<size_t>(i)]; }

const char* kScale =
    "track a note vel cva\n"
    "pattern p 4\n| C4 .5 1\n| D4 . 2\n| D4\n| ~E4\norder p\n";
}  // namespace

TEST_CASE("tracker registers with six banks of sixteen and six song outputs") {
  const dsp::ModuleInfo* info = dsp::find_module("tracker");
  REQUIRE(info);
  CHECK(info->output_index("voct1") == 0);
  CHECK(info->output_index("gate1") == 1);
  CHECK(info->output_index("cvb8") == col(7, kCvb));
  CHECK(info->output_index("cvd1") == col(0, kCvd));
  CHECK(info->output_index("voct16") == col(15, kVoct));
  CHECK(info->output_index("cvd16") == col(15, kCvd));
  CHECK(info->output_index("step") == kStep);
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

namespace {
// Samples of a period where the gate is low.
std::vector<int> lows(const std::vector<float>& g) {
  std::vector<int> v;
  for (size_t i = 0; i < g.size(); ++i)
    if (g[i] == 0.0f) v.push_back(static_cast<int>(i));
  return v;
}
std::vector<int> range(int from, int to) {
  std::vector<int> v;
  for (int i = from; i < to; ++i) v.push_back(i);
  return v;
}
}  // namespace

TEST_CASE("tracker: @D lands a row's change a fraction of the clock period late") {
  Trk t("track a note cva\npattern p 4\n| C4 1\n|\n| D4@.25 2\n| off@.5\norder p\n");
  t.period(col(0, kGate));                        // rows 0 and 1: the edges after
  t.period(col(0, kGate));                        // them measure a 100-sample row
  t.run(1.0f);
  CHECK(t.o(col(0, kCva)) == 1.0f);               // row 1's edge: nothing yet
  CHECK(t.o(kRow) == 2.0f);
  for (int s = 1; s < 25; ++s) t.run(1.0f);
  CHECK(t.o(col(0, kCva)) == 1.0f);
  CHECK(t.o(col(0, kGate)) == 1.0f);
  t.run(1.0f);                                    // sample 25: the delayed note
  CHECK(t.o(col(0, kCva)) == 2.0f);
  CHECK(t.o(col(0, kVoct)) == doctest::Approx(5.0f / 12));
  CHECK(t.o(col(0, kGate)) == 0.0f);              // re-articulated here
  t.run(1.0f);
  CHECK(t.o(col(0, kGate)) == 1.0f);
  for (int s = 27; s < 100; ++s) t.run(s < 50 ? 1.0f : 0.0f);
  CHECK(lows(t.period(col(0, kGate))) == range(50, 100));  // off@.5 cuts mid-row
}

TEST_CASE("tracker: *N ratchets spread N hits over the rest of the row") {
  Trk t("track a note\npattern p 5\n| x*4\n| x*4\n| x*4\n| x*2@.5\n| C4\norder p\n");
  CHECK(lows(t.period(col(0, kGate))).empty());   // no row measured yet: one hit
  CHECK(lows(t.period(col(0, kGate))) == std::vector<int>{0});
  CHECK(lows(t.period(col(0, kGate))) == std::vector<int>{0, 25, 50, 75});
  CHECK(lows(t.period(col(0, kGate))) == std::vector<int>{50, 75});
  CHECK(lows(t.period(col(0, kGate))) == std::vector<int>{0});
}

TEST_CASE("tracker: under two-row swing each row's hits fit that row") {
  Trk t("track a note\npattern p 2\n| x*2\n| x*2\norder |: p\n");
  auto row = [&](int len) {  // one row of `len` samples
    std::vector<float> g;
    for (int s = 0; s < len; ++s) {
      t.run(s < len / 2 ? 1.0f : 0.0f);
      g.push_back(t.o(col(0, kGate)));
    }
    return lows(g);
  };
  for (int i = 0; i < 2; ++i) row(120), row(80);
  CHECK(row(120) == std::vector<int>{0, 60});
  CHECK(row(80) == std::vector<int>{0, 40});
}

TEST_CASE("tracker: ?P plays a note with probability P, repeatably per seed") {
  std::string never = "track a note\npattern p 2\n| x?0\n| x\norder p\n";
  Trk n(never);
  CHECK(lows(n.period(col(0, kGate))).size() == 100);  // skipped: silent all row
  CHECK(lows(n.period(col(0, kGate))) == std::vector<int>{});  // the next note rolls again

  const std::string coin = "track a note\npattern p 1\n| x?.5\norder |: p\n";
  auto pattern = [&](double seed) {
    Trk t(coin, seed);
    std::string heard;
    for (int i = 0; i < 64; ++i) heard += t.period(col(0, kGate))[10] > 0.0f ? 'x' : '.';
    return heard;
  };
  const std::string a = pattern(1), b = pattern(1), c = pattern(2);
  CHECK(a == b);
  CHECK(a != c);
  const auto hits = std::count(a.begin(), a.end(), 'x');
  CHECK(hits > 16);
  CHECK(hits < 48);

  Trk t(coin);  // reset rewinds the dice: the same pass again
  std::string first, again;
  for (int i = 0; i < 8; ++i) first += t.period(col(0, kGate))[10] > 0.0f ? 'x' : '.';
  t.in[1] = 1.0f;
  for (int i = 0; i < 8; ++i) {
    again += t.period(col(0, kGate))[10] > 0.0f ? 'x' : '.';
    t.in[1] = 0.0f;
  }
  CHECK(first == again);
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

TEST_CASE("tracker loader: a cable from an undeclared track is an error") {
  TempDir d;
  d.write("songs/a.trk", ramp(0.0f));
  const std::string src = std::string(kPatch) + "song.gate2 -> out.right\n";
  const std::string patch = d.write("p.patch", src);
  auto b = graph::build_patch(src, patch, 48000.0);
  REQUIRE(b.compiled.errors.size() == 1);
  CHECK(b.compiled.errors[0].line == 5);
  CHECK(b.compiled.errors[0].msg.find("reads track 2, but 'songs/a.trk' declares 1") !=
        std::string::npos);
}

TEST_CASE("tracker loader: every bank's ports are checked against the declared tracks") {
  TempDir d;
  std::string trk;
  for (int t = 1; t <= 12; ++t) trk += "track t" + std::to_string(t) + " cvd\n";
  d.write("songs/a.trk", trk + "pattern p 1\n| 1\norder p\n");
  const std::string ok = std::string(kPatch) + "song.cvd12 -> out.right\n";
  CHECK(graph::build_patch(ok, d.write("p.patch", ok), 48000.0).compiled.ok());
  for (const char* port : {"voct13", "gate16", "cvd13", "cvb14"}) {
    CAPTURE(port);
    const std::string src = std::string(kPatch) + "song." + port + " -> out.right\n";
    auto b = graph::build_patch(src, d.write("p.patch", src), 48000.0);
    REQUIRE(b.compiled.errors.size() == 1);
    CHECK(b.compiled.errors[0].msg.find("declares 12") != std::string::npos);
  }
  const std::string past = std::string(kPatch) + "song.cvd17 -> out.right\n";  // not a port
  auto b = graph::build_patch(past, d.write("p.patch", past), 48000.0);
  REQUIRE(b.compiled.errors.size() == 1);
  CHECK(b.compiled.errors[0].msg.find("declares") == std::string::npos);
}

TEST_CASE("tracker: track 16's cvd reaches its port") {
  std::string trk;
  for (int t = 1; t <= 16; ++t) trk += "track t" + std::to_string(t) + " note cvd\n";
  trk += "pattern p 1 t16\n| C4 E4\norder p\n";
  Trk t(trk);
  t.tick();
  CHECK(t.o(col(15, kVoct)) == doctest::Approx(0.25f));
  CHECK(t.o(col(15, kCvd)) == doctest::Approx(7.0f / 12));
  CHECK(t.o(col(15, kGate)) == 1.0f);
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
