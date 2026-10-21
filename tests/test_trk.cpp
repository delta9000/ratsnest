// The .trk song format: every syntax element, every diagnostic with
// line:col, and mutation fuzzing (rule 6: report, never crash or hang).
#include <cmath>
#include <cstdint>
#include <string>
#include <doctest/doctest.h>
#include "io/trk.h"

using ms::io::parse_trk;
using ms::io::TrkRow;
using ms::io::TrkSong;

namespace {

TrkSong ok(const std::string& text) {
  auto r = parse_trk(text);
  INFO(text);
  REQUIRE_MESSAGE(r.ok(), (r.errors.empty() ? "" : r.errors[0].msg));
  return r.song;
}

const TrkRow& at(const TrkSong& s, int row, int track) {
  return s.rows[static_cast<size_t>(row * s.tracks + track)];
}

// The first error's line, col and message fragment.
void fails(const std::string& text, int line, int col, const std::string& msg) {
  auto r = parse_trk(text);
  INFO(text);
  REQUIRE(r.errors.size() == 1);
  CHECK(r.errors[0].line == line);
  CHECK(r.errors[0].col == col);
  CHECK_MESSAGE(r.errors[0].msg.find(msg) != std::string::npos, r.errors[0].msg);
  CHECK(r.song.length() == 0);
}

const char* kHead = "track a note vel cva cvb\n";

}  // namespace

TEST_CASE("trk: a minimal song flattens to one row per pattern row") {
  auto s = ok("track a note\npattern p 4\n| C4\norder p\n");
  CHECK(s.tracks == 1);
  CHECK(s.length() == 4);
  CHECK(s.steps() == 1);
  CHECK(s.loop == -1);
  CHECK(at(s, 0, 0).voct == doctest::Approx(0.25f));  // C4 is 3 semitones over A3
  CHECK(at(s, 0, 0).on);
  CHECK(at(s, 0, 0).retrig);
  CHECK(at(s, 3, 0).on);        // a note holds until changed
  CHECK_FALSE(at(s, 1, 0).retrig);
}

TEST_CASE("trk: note spellings are the DSL's, always v/oct") {
  auto s = ok(std::string(kHead) +
              "pattern p 6\n| A3\n| Cs4\n| Bb3\n| A4v\n| -0.5\n| .25\norder p\n");
  CHECK(at(s, 0, 0).voct == doctest::Approx(0.0f));
  CHECK(at(s, 1, 0).voct == doctest::Approx(4.0f / 12));
  CHECK(at(s, 2, 0).voct == doctest::Approx(1.0f / 12));
  CHECK(at(s, 3, 0).voct == doctest::Approx(1.0f));
  CHECK(at(s, 4, 0).voct == doctest::Approx(-0.5f));
  CHECK(at(s, 5, 0).voct == doctest::Approx(0.25f));
}

TEST_CASE("trk: empty cells, off, x, slur and held values") {
  auto s = ok(std::string(kHead) +
              "pattern p 8\n"
              "| D4 .5 1 C4\n"   // 0 note, vel, cva, cvb as a note name
              "| ...\n"          // 1 empty: everything holds
              "| ~E4\n"          // 2 slur: pitch moves, no re-articulation
              "| x . 2\n"        // 3 x: re-articulate at the held pitch
              "| ===\n"          // 4 off
              "| --- .9\n"       // 5 empty note, vel changes while off
              "| ~F4\n"          // 6 slur from silence starts a note
              "| off\n"
              "order p\n");
  CHECK(at(s, 1, 0).voct == doctest::Approx(5.0f / 12));
  CHECK(at(s, 1, 0).vel == doctest::Approx(0.5f));
  CHECK(at(s, 1, 0).cva == doctest::Approx(1.0f));
  CHECK(at(s, 1, 0).cvb == doctest::Approx(0.25f));
  CHECK(at(s, 1, 0).on);
  CHECK_FALSE(at(s, 1, 0).retrig);
  CHECK(at(s, 2, 0).voct == doctest::Approx(7.0f / 12));
  CHECK(at(s, 2, 0).on);
  CHECK_FALSE(at(s, 2, 0).retrig);
  CHECK(at(s, 3, 0).retrig);
  CHECK(at(s, 3, 0).voct == doctest::Approx(7.0f / 12));
  CHECK(at(s, 3, 0).cva == doctest::Approx(2.0f));
  CHECK_FALSE(at(s, 4, 0).on);
  CHECK(at(s, 5, 0).vel == doctest::Approx(0.9f));
  CHECK_FALSE(at(s, 5, 0).on);
  CHECK(at(s, 6, 0).on);
  CHECK_FALSE(at(s, 6, 0).retrig);
  CHECK_FALSE(at(s, 7, 0).on);
}

TEST_CASE("trk: defaults before anything is written") {
  auto s = ok(std::string(kHead) + "pattern p 1\norder p\n");
  CHECK(at(s, 0, 0).voct == 0.0f);
  CHECK(at(s, 0, 0).vel == 1.0f);
  CHECK(at(s, 0, 0).cva == 0.0f);
  CHECK(at(s, 0, 0).cvb == 0.0f);
  CHECK_FALSE(at(s, 0, 0).on);
}

TEST_CASE("trk: fields follow the track's declared order; trailing ones may be omitted") {
  auto s = ok("track k note\ntrack m cvb cva\n"
              "pattern p 2\n| x | 3 4\n| | 5\norder p\n");
  CHECK(s.tracks == 2);
  CHECK(at(s, 0, 0).on);
  CHECK(at(s, 0, 1).cvb == doctest::Approx(3.0f));
  CHECK(at(s, 0, 1).cva == doctest::Approx(4.0f));
  CHECK(at(s, 1, 1).cvb == doctest::Approx(5.0f));
  CHECK(at(s, 1, 1).cva == doctest::Approx(4.0f));
}

TEST_CASE("trk: row labels make sparse patterns; unlabelled rows follow on") {
  auto s = ok("track a cva\npattern p 8\n02 | 1\n| 2\n6 | 3\norder p\n");
  CHECK(at(s, 0, 0).cva == 0.0f);
  CHECK(at(s, 2, 0).cva == 1.0f);
  CHECK(at(s, 3, 0).cva == 2.0f);
  CHECK(at(s, 5, 0).cva == 2.0f);
  CHECK(at(s, 6, 0).cva == 3.0f);
  CHECK(at(s, 7, 0).cva == 3.0f);
}

TEST_CASE("trk: comments, blank lines, trailing bars and CRLF") {
  auto s = ok("# a song\r\n\r\ntrack a cva   # one lane\r\n"
              "pattern p 2 # two rows\r\n|1|# trailing bar then comment\r\n"
              "| 2 |\r\norder p # done\r\n");
  CHECK(at(s, 0, 0).cva == 1.0f);
  CHECK(at(s, 1, 0).cva == 2.0f);
}

TEST_CASE("trk: a pattern header picks and orders its tracks") {
  auto s = ok("track a cva\ntrack b cva\ntrack c cva\n"
              "pattern p 1 c a\n| 3 | 1\norder p\n");
  CHECK(at(s, 0, 0).cva == 1.0f);
  CHECK(at(s, 0, 1).cva == 0.0f);
  CHECK(at(s, 0, 2).cva == 3.0f);
}

TEST_CASE("trk: layers stack; the first sets the length, the rest loop") {
  auto s = ok("track k note\ntrack h cva\n"
              "pattern long 6 h\n| 1\n03 | 2\n"
              "pattern beat 2 k\n| x\n| off\n"
              "order long+beat\n");
  CHECK(s.length() == 6);
  CHECK(s.step_pattern[0] == 0);
  for (int r = 0; r < 6; ++r) {
    CAPTURE(r);
    CHECK(at(s, r, 0).on == (r % 2 == 0));
    CHECK(at(s, r, 0).retrig == (r % 2 == 0));
  }
  CHECK(at(s, 4, 1).cva == 2.0f);
}

TEST_CASE("trk: a looping layer longer than the step is an error, not a cut") {
  const std::string h = "track h note\ntrack m note\npattern harm 64 h\npattern mel 128 m\n";
  ok(h + "order mel+harm\n");  // longest first: harm loops twice
  fails(h + "order harm+mel\n", 5, 12,
        "layer 'mel' (128 rows) is longer than the step (64 rows, set by 'harm'); "
        "list the longest layer first");
  // A section is checked where it is defined.
  fails(h + "section v harm+mel\norder v\n", 5, 16, "layer 'mel' (128 rows)");
  // Overlays keep their own rule: they may run past the step and are cut.
  auto s = ok(h + "order harm+mel@60\n");
  CHECK(s.length() == 64);
}

TEST_CASE("trk: order lines accumulate, repeat, and mark a loop point") {
  auto s = ok("track a cva\npattern a1 2\n| 1\npattern b2 3\n| 2\n"
              "order a1*2\norder |: b2 a1*3\n");
  CHECK(s.steps() == 6);
  CHECK(s.loop == 2);
  CHECK(s.length() == 2 + 2 + 3 + 6);
  const std::vector<int> starts{0, 2, 4, 7, 9, 11, 13};
  CHECK(s.step_start == starts);
  const std::vector<int> pats{0, 0, 1, 0, 0, 0};
  CHECK(s.step_pattern == pats);
}

TEST_CASE("trk: empty tracks in a step hold, and state carries across steps") {
  auto s = ok("track a note cva\ntrack b cva\n"
              "pattern one 1 a\n| D4 7\npattern two 1 b\n| 9\norder one two\n");
  CHECK(at(s, 1, 0).on);  // track a not in `two`: its note holds
  CHECK(at(s, 1, 0).cva == 7.0f);
  CHECK_FALSE(at(s, 1, 0).retrig);
  CHECK(at(s, 1, 1).cva == 9.0f);
}

TEST_CASE("trk: diagnostics carry line and column") {
  const std::string h = "track a note vel\n";
  fails("trak a note\n", 1, 1, "unknown directive 'trak'");
  fails("track a note\n  C4\n", 2, 3, "unknown directive 'C4'");
  fails("track a note\n| C4\n", 2, 1, "row outside a pattern");
  fails("pattern p 4\n", 1, 1, "declare tracks before patterns");
  fails("track A note\n", 1, 7, "lowercase");
  fails("track a\n", 1, 1, "needs a name and fields");
  fails("track a note pitch\n", 1, 14, "unknown field 'pitch'");
  fails("track a note note\n", 1, 14, "listed twice");
  fails("track a note\ntrack a vel\n", 2, 7, "duplicate track 'a'");
  fails("track a note\npattern p 1\ntrack b note\n", 3, 1, "before the first pattern");
  fails(std::string() +
            "track a cva\ntrack b cva\ntrack c cva\ntrack d cva\ntrack e cva\n"
            "track f cva\ntrack g cva\ntrack h cva\ntrack i cva\n",
        9, 1, "at most 8 tracks");
  fails(h + "pattern p\n", 2, 1, "needs a name and a row count");
  fails(h + "pattern p 0\n", 2, 11, "1..1024");
  fails(h + "pattern p 2000\n", 2, 11, "1..1024");
  fails(h + "pattern p 4 b\n", 2, 13, "unknown track 'b'");
  fails(h + "pattern p 4 a a\n", 2, 15, "listed twice");
  fails(h + "pattern p 4\npattern p 4\n", 3, 9, "duplicate name 'p'");
  fails(h + "pattern p 4\n| C#4\n", 3, 3, "Cs4 for sharp");
  fails(h + "pattern p 4\n| H4\n", 3, 3, "is not a note");
  fails(h + "pattern p 4\n| ~\n", 3, 3, "is not a note");
  fails(h + "pattern p 4\n| C4 1.5\n", 3, 6, "vel must be a number in 0..1");
  fails(h + "pattern p 4\n| .6 C4\n", 3, 6, "follow the track's field order");
  fails(h + "pattern p 4\n| e4\n", 3, 3, "uppercase");
  fails(h + "pattern p 4\n| C4 loud\n", 3, 6, "not a number or note name for vel");
  fails(h + "pattern p 4\n| C4 1 1\n", 3, 8, "cell has 3 values; track 'a' holds 2 (note, vel)");
  fails(h + "pattern p 4\n| C4 | D4\n", 3, 6, "more cells than pattern 'p' has tracks");
  fails(h + "pattern p 4\n04 | C4\n", 3, 1, "row 4 is outside pattern 'p'");
  fails(h + "pattern p 4\n| C4\n| C4\n| C4\n| C4\n| C4\n", 7, 1, "outside pattern 'p'");
  fails(h + "pattern p 4\n02 | C4\n01 | C4\n", 4, 1, "row labels must increase");
  fails(h + "pattern p 4\n02 C4\n", 3, 1, "expected '|' after the row number");
  fails(h + "pattern p 4\n| C4 |: D4\n", 3, 6, "expected '|' between cells");
  fails(h + "pattern p 4\n", 1, 1, "no order list");
  fails(h + "pattern p 4\norder q\n", 3, 7, "unknown pattern 'q'");
  fails(h + "pattern verse 4\norder vers\n", 3, 7, "did you mean 'verse'");
  fails(h + "pattern p 4\norder p*0\n", 3, 8, "'*N' with N in 1..4096");
  fails(h + "pattern p 4\norder p*x\n", 3, 8, "'*N'");
  fails(h + "pattern p 4\norder p |:\n", 3, 9, "needs an order entry after it");
  fails(h + "pattern p 4\norder |: p |: p\n", 3, 12, "only one loop point");
  fails(h + "pattern p 4\npattern q 4\norder p+q\n", 4, 9,
        "layers 'p' and 'q' both write track 'a'");
  fails(h + "pattern p 1024\norder p*65\n", 3, 7, "longer than 65536 rows");
}

TEST_CASE("trk: ^N in the order list transposes notes and note-name cvs") {
  auto s = ok("track a note cva cvb\ntrack b note\n"
              "pattern p 2 a\n| C4 E4 .5\n| 0\npattern q 1 b\n| A3\n"
              "order p^5 p^-2+q^12 p\n");
  CHECK(at(s, 0, 0).voct == doctest::Approx(8.0f / 12));   // C4 + 5
  CHECK(at(s, 0, 0).cva == doctest::Approx(12.0f / 12));   // E4 + 5: a pitch
  CHECK(at(s, 0, 0).cvb == doctest::Approx(0.5f));         // a level: untouched
  CHECK(at(s, 1, 0).voct == doctest::Approx(5.0f / 12));   // a numeric note moves too
  CHECK(at(s, 2, 0).voct == doctest::Approx(1.0f / 12));   // C4 - 2
  CHECK(at(s, 2, 0).cva == doctest::Approx(5.0f / 12));
  CHECK(at(s, 2, 1).voct == doctest::Approx(1.0f));        // q^12, layer-local
  CHECK(at(s, 4, 0).voct == doctest::Approx(0.25f));       // untransposed again
  fails("track a note\npattern p 1\norder p^49\n", 3, 8, "'^N' with N in -48..48");
  fails("track a note\npattern p 1\norder p^x*2\n", 3, 8, "'^N'");
  fails("track a note\npattern p 1\norder q^2\n", 3, 7, "unknown pattern 'q'");
}

TEST_CASE("trk: note length from the track default or the cell") {
  auto s = ok("track a note vel len 1\ntrack b note\n"
              "pattern p 8\n"
              "| C4 .5 | x:3\n"   // 0 a: one row; b: three rows
              "| .     | .\n"     // 1 a off by length
              "| ~D4:4 | C4:2\n"  // 2 a slur sounds 4 rows; b's next note cuts its length
              "| .\n"             // 3
              "| x:2\n"           // 4 a re-articulates before its length runs out
              "|\n|\n|\n"
              "order p\n");
  CHECK(at(s, 0, 0).on);
  CHECK_FALSE(at(s, 1, 0).on);
  CHECK(at(s, 1, 0).vel == doctest::Approx(0.5f));
  CHECK(at(s, 2, 0).on);
  CHECK_FALSE(at(s, 2, 0).retrig);
  CHECK(at(s, 3, 0).on);
  CHECK(at(s, 4, 0).retrig);
  CHECK(at(s, 5, 0).on);
  CHECK_FALSE(at(s, 6, 0).on);  // x:2 from row 4
  CHECK(at(s, 1, 1).on);
  CHECK(at(s, 2, 1).retrig);    // b: C4:2 at row 2 replaces x:3 (which ran to 3)
  CHECK(at(s, 3, 1).on);
  CHECK_FALSE(at(s, 4, 1).on);
  CHECK_FALSE(at(s, 7, 1).on);
}

TEST_CASE("trk: a looping layer's note length counts song rows") {
  // A 3-row loop under 4-row steps whose hit falls on layer row 2: the one
  // on step row 2 rings into the next step's first row.
  auto s = ok("track k note\ntrack h cva\n"
              "pattern long 4 h\n| 1\npattern hit 3 k\n02 | x:2\norder long+hit*2\n");
  const bool on[] = {0, 0, 1, 1, 0, 0, 1, 1};
  for (int r = 0; r < 8; ++r) {
    CAPTURE(r);
    CHECK(at(s, r, 0).on == on[r]);
  }
}

TEST_CASE("trk: note length diagnostics") {
  fails("track a note len\n", 1, 14, "'len' needs a value");
  fails("track a note len 0\n", 1, 18, "row count in 1..4096");
  fails("track a len 2\n", 1, 1, "needs a name and fields");
  fails("track a note\npattern p 2\n| C4:0\n", 3, 5, "':L' with L in 1..4096");
  fails("track a note\npattern p 2\n| C4:\n", 3, 5, "':L'");
  fails("track a note\npattern p 2\n| :3\n", 3, 3, "is not a note");
}

TEST_CASE("trk: chord tones from a source track follow its chords") {
  const std::string h =
      "track pad note cva cvb\ntrack arp note vel from pad\ntrack lvl cva cvb from pad\n"
      "pattern chords 4 pad\n| D3 F4 A3\n02 | Bb2 D4 F3\n"
      "pattern arp 2 arp lvl\n| t1+12 .8 | t3-12 .5\n| ~t2:1 | .25 t2\n";
  const float d3 = -7.0f / 12, f4 = 8.0f / 12, a3 = 0.0f, bb2 = -11.0f / 12, d4 = 5.0f / 12;
  auto s = ok(h + "order chords+arp chords^2+arp^-12\n");
  CHECK(at(s, 0, 1).voct == doctest::Approx(d3 + 1));
  CHECK(at(s, 0, 1).retrig);
  CHECK(at(s, 0, 1).vel == doctest::Approx(0.8f));
  CHECK(at(s, 0, 2).cva == doctest::Approx(a3 - 1));  // cva reads t3, an octave down
  CHECK(at(s, 0, 2).cvb == doctest::Approx(0.5f));
  CHECK(at(s, 1, 1).voct == doctest::Approx(f4));     // ~t2: slur to the third
  CHECK_FALSE(at(s, 1, 1).retrig);
  CHECK(at(s, 1, 2).cva == doctest::Approx(0.25f));   // a plain level
  CHECK(at(s, 1, 2).cvb == doctest::Approx(f4));
  CHECK(at(s, 2, 1).voct == doctest::Approx(bb2 + 1));  // the riff loops under the next chord
  CHECK(at(s, 2, 1).on);  // ~t2:1 ended on row 2, the new note starts there
  CHECK(at(s, 3, 1).voct == doctest::Approx(d4));
  // Step 2: the source moves up 2, the arp's own ^-12 adds on top.
  CHECK(at(s, 4, 1).voct == doctest::Approx(d3 + 2.0f / 12 + 1 - 1));
  CHECK(at(s, 4, 2).cva == doctest::Approx(a3 + 2.0f / 12 - 1 - 1));
  CHECK(at(s, 5, 2).cva == doctest::Approx(0.25f));  // levels never move
}

TEST_CASE("trk: chord tone diagnostics") {
  const std::string h = "track pad note cva\n";
  fails(h + "track arp note from\n", 2, 16, "'from' needs a value");
  fails(h + "track arp note from pads\n", 2, 21, "did you mean 'pad'");
  fails(h + "track arp note from arp\n", 2, 21, "a source track is declared earlier");
  fails(h + "track lvl cva\ntrack arp note from lvl\n", 3, 21, "no note field");
  fails(h + "track arp note\npattern p 1 arp\n| t1\n", 4, 3, "has no source");
  fails(h + "track arp note from pad\npattern p 1 arp\n| t3\n", 4, 3, "reads cvb, which track 'pad'");
  fails(h + "track arp note from pad\npattern p 1 arp\n| t1+x\n", 4, 3, "t1-12, t3+7");
  fails(h + "track arp note from pad\npattern p 1 arp\n| t1+\n", 4, 3, "is not a chord tone");
  fails(h + "track arp note from pad\npattern p 1 arp\n| t1+49\n", 4, 3, "is not a chord tone");
  fails(h + "track arp note from pad\npattern p 1 arp\n| t4\n", 4, 3, "is not a note");
  fails(h + "track arp note vel from pad\npattern p 1 arp\n| t1 t2\n", 4, 6, "for vel");
}

TEST_CASE("trk: an @R overlay plays once over the groove") {
  auto s = ok("track k note\ntrack h cva\n"
              "pattern bar 8 h\n| 1\n"
              "pattern groove 2 k\n| x\n| off\n"
              "pattern fill 3 k h\n| x | 5\n| x\n"
              "order bar+groove+fill@4^2 bar+fill^2@6\n");
  // Rows 0..3: the groove alone.
  CHECK(at(s, 2, 0).retrig);
  CHECK_FALSE(at(s, 3, 0).on);
  CHECK(at(s, 3, 1).cva == 1.0f);
  // Row 4: fill row 0 replaces the groove's x and writes h.
  CHECK(at(s, 4, 0).retrig);
  CHECK(at(s, 4, 1).cva == 5.0f);
  // Row 5: fill row 1 (x) overrides the groove's off.
  CHECK(at(s, 5, 0).on);
  CHECK(at(s, 5, 0).retrig);
  // Row 6: fill row 2 is empty, so the groove's x shows through; row 7 is
  // past the fill and the groove's off plays.
  CHECK(at(s, 6, 0).retrig);
  CHECK_FALSE(at(s, 7, 0).on);
  // Step 2: the fill starts on row 6 and does not loop back to row 0.
  CHECK_FALSE(at(s, 8, 0).on);
  CHECK(at(s, 14, 0).retrig);
  CHECK(at(s, 14, 1).cva == 5.0f);
}

TEST_CASE("trk: overlay diagnostics") {
  const std::string h = "track a note\npattern p 4\npattern q 2\n";
  fails(h + "order q@1+p\n", 4, 8, "the first layer sets the step length");
  fails(h + "order p+q@4\n", 4, 11, "past the step's end ('p' has 4 rows: 0..3)");
  fails(h + "order p+q@x\n", 4, 11, "decimal");
  fails(h + "order p+q^x@1\n", 4, 10, "'^N'");
  fails(h + "order p+q\n", 4, 9, "only an '@' overlay may share tracks");
}

TEST_CASE("trk: drum lanes write one row per character") {
  auto s = ok("track kick note\ntrack hat note vel len 1\ntrack bass note\n"
              "pattern beat 8\n"
              "lane kick x... x.0.\n"
              "lane hat  .9.x 3...\n"
              "| . | . | C2\n"
              "04 | . | . | D2\n"
              "order beat\n");
  const bool kick[] = {1, 1, 1, 1, 1, 1, 0, 0};
  for (int r = 0; r < 8; ++r) {
    CAPTURE(r);
    CHECK(at(s, r, 0).on == kick[r]);
    CHECK(at(s, r, 0).retrig == (r == 0 || r == 4));
  }
  CHECK(at(s, 1, 1).retrig);
  CHECK(at(s, 1, 1).vel == doctest::Approx(1.0f));
  CHECK_FALSE(at(s, 2, 1).on);  // len 1
  CHECK(at(s, 3, 1).vel == doctest::Approx(1.0f));  // x keeps the vel
  CHECK(at(s, 4, 1).vel == doctest::Approx(3.0f / 9));
  CHECK(at(s, 4, 2).voct == doctest::Approx(-19.0f / 12));  // rows and lanes mix
  // A digit on a track without vel is a plain hit; a lane cell fills an
  // empty row cell and vice versa.
  s = ok("track k note\ntrack v cva\npattern p 2\n| . | 3\nlane k 5\norder p\n");
  CHECK(at(s, 0, 0).retrig);
  CHECK(at(s, 0, 1).cva == 3.0f);
}

TEST_CASE("trk: lane diagnostics") {
  const std::string h = "track k note vel\ntrack b note\npattern p 4 k\n";
  fails("track k note\nlane k x\n", 2, 1, "lane outside a pattern");
  fails(h + "lane k\n", 4, 1, "lane needs a track and its steps");
  fails(h + "lane b x\n", 4, 6, "track 'b' is not in pattern 'p'");
  fails(h + "lane kk x\n", 4, 6, "did you mean 'k'");
  fails(h + "lane k x... x\n", 4, 13, "lane is longer than pattern 'p' (4 rows)");
  fails(h + "lane k x.o.\n", 4, 10, "'o' is not a lane step");
  fails(h + "| C4\nlane k x\n", 5, 8, "row 0 of track 'k' is already written");
  fails(h + "| . .5\nlane k 5\n", 5, 8, "already written");
  fails(h + "lane k 5\n| . .5\n", 5, 5, "already written by a lane");
  fails(h + "lane k x\n| C4\n", 5, 3, "already written by a lane");
}

TEST_CASE("trk: BARSxROWS patterns take bar:row labels and overlay starts") {
  auto s = ok("track a cva\ntrack k note\n"
              "pattern verse 4x16 a\n1:00 | 1\n2:4 | 2\n3:06 | 3\n40 | 4\n4:15 | 5\n"
              "pattern fill 2 k\n| x\n"
              "order verse+fill@4:08\n");
  CHECK(s.length() == 64);
  CHECK(at(s, 19, 0).cva == 1.0f);
  CHECK(at(s, 20, 0).cva == 2.0f);   // 2:4 = 16 + 4
  CHECK(at(s, 38, 0).cva == 3.0f);   // 3:06 = 32 + 6
  CHECK(at(s, 40, 0).cva == 4.0f);   // plain numbers still work
  CHECK(at(s, 63, 0).cva == 5.0f);
  CHECK_FALSE(at(s, 55, 1).on);
  CHECK(at(s, 56, 1).retrig);        // 4:08 = 48 + 8
}

TEST_CASE("trk: bar diagnostics") {
  const std::string h = "track a cva\n";
  fails(h + "pattern p 64\n2:00 | 1\n", 3, 1, "declare 'pattern p BARSxROWS'");
  fails(h + "pattern p 4x16\n5:00 | 1\n", 3, 1, "not a bar:row of pattern 'p' (bars 1..4, rows 0..15)");
  fails(h + "pattern p 4x16\n0:00 | 1\n", 3, 1, "bars 1..4");
  fails(h + "pattern p 4x16\n1:16 | 1\n", 3, 1, "rows 0..15");
  fails(h + "pattern p 4x16\n2:00 | 1\n1:15 | 1\n", 4, 1, "row labels must increase");
  fails(h + "pattern p 4x16\n2x | 1\n", 3, 1, "decimal integer or bar:row");
  fails(h + "pattern p 2x1000\n", 2, 11, "at most 1024 rows");
  fails(h + "pattern p 4x\n", 2, 11, "BARSxROWS");
  fails(h + "pattern p 64\npattern q 2\norder p+q@1:00\n", 4, 11, "declare 'pattern p BARSxROWS'");
}

TEST_CASE("trk: sections name layer groups; ^N moves all their layers") {
  auto s = ok("track a note\ntrack b note\ntrack c cva\n"
              "section verse p^2+q\n"  // patterns may come later
              "pattern p 4 a\n| C4\npattern q 2 b\n| A3\npattern f 1 c\n| 7\n"
              "order verse verse^-2+f@3 |: verse*2\n");
  CHECK(s.steps() == 4);
  CHECK(s.loop == 2);
  CHECK(s.step_pattern[0] == 0);
  CHECK(at(s, 0, 0).voct == doctest::Approx(5.0f / 12));
  CHECK(at(s, 2, 1).retrig);  // q loops inside the section's step
  CHECK(at(s, 4, 0).voct == doctest::Approx(3.0f / 12));
  CHECK(at(s, 4, 1).voct == doctest::Approx(-2.0f / 12));
  CHECK(at(s, 6, 2).cva == 0.0f);
  CHECK(at(s, 7, 2).cva == 7.0f);  // an overlay added to a section's group
  CHECK(at(s, 8, 0).voct == doctest::Approx(5.0f / 12));
  // A section's ^N moves a dependent's written pitches, but its chord tones
  // only once, through their source; the layer's own ^-12 still applies.
  s = ok("track h note\ntrack d note cva from h\nsection v c+r^-12\n"
         "pattern c 1 h\n| C4\npattern r 1 d\n| t1 E4\norder v^2\n");
  CHECK(at(s, 0, 1).voct == doctest::Approx(0.25f + 2.0f / 12 - 1));
  CHECK(at(s, 0, 1).cva == doctest::Approx(7.0f / 12 + 2.0f / 12 - 1));
}

TEST_CASE("trk: section diagnostics") {
  const std::string h = "track a note\ntrack b note\npattern p 4 a\npattern q 4 b\n";
  fails(h + "section\n", 5, 1, "section needs a name and one layer group");
  fails(h + "section v p q\n", 5, 1, "one layer group");
  fails(h + "section V p\n", 5, 9, "lowercase");
  fails(h + "section p q\n", 5, 9, "duplicate name 'p' (patterns and sections share names)");
  fails(h + "section v p\npattern v 2\n", 6, 9, "duplicate name 'v'");
  fails(h + "section v p+r\norder v\n", 5, 13, "unknown pattern 'r'");
  fails(h + "section v p+q\norder v^x\n", 6, 8, "'^N'");
  fails(h + "section verse p+q\norder vers\n", 6, 7, "did you mean 'verse'");
  fails(h + "section v p\nsection w v+q\norder w\n", 6, 11, "holds patterns, not sections");
  fails(h + "section v p\norder q+v@2\n", 6, 10, "a section cannot be an '@' overlay");
  fails(h + "section v p+q\norder v+p\n", 6, 9, "both write track 'a'");
  fails(h + "pattern r 2 a\nsection v r+q@1\norder p+v\n", 7, 9, "only an '@' overlay may share");
  fails(h + "pattern r 2 a\nsection v r+p@6\norder q+v\n", 6, 15, "past the step's end ('r' has 2 rows");
}

TEST_CASE("trk: a trailing '>' ramps a value to the next one in its field") {
  auto s = ok("track a note vel cva\ntrack b cvb\n"
              "pattern p 8\n"
              "| C4 .2> A3> | 1>\n"
              "02 | . . . | 3\n"
              "04 | . .6 A4 | 0\n"
              "| . . 2> \n"
              "07 | . . 4\n"
              "order p p^12\n");
  const float vel[] = {.2f, .3f, .4f, .5f, .6f, .6f, .6f, .6f};
  for (int r = 0; r < 8; ++r) {
    CAPTURE(r);
    CHECK(at(s, r, 0).vel == doctest::Approx(vel[r]));
  }
  CHECK(at(s, 2, 0).cva == doctest::Approx(0.5f));  // A3 to A4 over four rows
  CHECK(at(s, 1, 1).cvb == doctest::Approx(2.0f));
  CHECK(at(s, 3, 1).cvb == doctest::Approx(3.0f));  // the ramp ended at row 2
  CHECK(at(s, 6, 0).cva == doctest::Approx(3.0f));  // 2 to 4 over rows 5..7
  CHECK(at(s, 10, 0).cva == doctest::Approx(1.5f));  // a pitch ramp transposes
  CHECK(at(s, 14, 0).cva == doctest::Approx(3.0f));  // a level ramp does not
}

TEST_CASE("trk: ramp diagnostics") {
  fails("track a cva\npattern p 4\n| 1>\n| 2\npattern q 4\n| 1\n03 | 5>\norder p\n", 7, 6,
        "a ramp '>' glides to the next number or note written in its cva field later in pattern 'q'");
  fails("track a note vel\npattern p 4\n| C4 .5>\n| D4\norder p\n", 3, 6, "in its vel field");
  fails("track a note\npattern p 4\n| C4>\n", 3, 3, "is not a note");
  fails("track a cva\npattern p 4\n| >\n", 3, 3, "not a number or note name for cva");
  fails("track a note\ntrack b note cva from a\npattern p 4 b\n| . 1>\n| . t1\norder p\n", 4, 5,
        "none follows");
}

TEST_CASE("trk: the composing example from the design spec") {
  auto s = ok("# every composing feature in one song\n"
              "track harm note cva cvb\n"
              "track arp  note vel cva from harm len 1\n"
              "track kick note vel len 1\n"
              "pattern prog 2x16 harm\n"
              "1:00 | D3  F4 A3\n"
              "2:00 | Bb2 D4 F3\n"
              "pattern arp 8 arp\n"
              "| t1+12 .3> t3\n"
              "4 | ~t2+12:3 .7 t1-12\n"
              "pattern beat 16 kick\n"
              "lane kick 9..5 x..5 9..5 x.0.\n"
              "pattern fill 4 kick\n"
              "lane kick 9999\n"
              "section verse prog+arp+beat\n"
              "order verse*2 |: verse^5+fill@2:12 verse^-2\n");
  CHECK(s.steps() == 4);
  CHECK(s.loop == 2);
  CHECK(at(s, 2, 1).vel == doctest::Approx(0.5f));
  CHECK(at(s, 16, 1).voct == doctest::Approx(1.0f - 11.0f / 12));    // Bb2 + 12
  CHECK(at(s, 64 + 16, 1).voct == doctest::Approx(1.0f - 6.0f / 12)); // ^5
  CHECK(at(s, 64 + 29, 2).retrig);                                    // the fill
  CHECK(at(s, 96, 0).voct == doctest::Approx(-9.0f / 12));            // D3 - 2
}

TEST_CASE("trk: the longest song fits exactly") {
  auto s = ok("track a cva\npattern p 1024\norder p*64\n");
  CHECK(s.length() == 65536);
}

// Byte-level mutations of a realistic song: every result either parses or
// reports exactly one positioned error, and never crashes or hangs (the
// sanitizer build turns any memory error into a failure).
TEST_CASE("trk: mutation fuzz never crashes and always positions its error") {
  const std::string songs[] = {
      "# fuzz seed\ntrack kick note\ntrack bass note vel cva\ntrack harm note cva cvb\n"
      "pattern beat 4 kick\n00 | x\n02 | x\n"
      "pattern verse 8 bass harm\n00 | D2 .9 1 | D3 F4 A3\n| ~E2\n04 | off | Bb2 D4 F3\n"
      "order verse*2 |: verse+beat*3\n",
      "track harm note cva cvb\ntrack arp note vel cva from harm len 1\ntrack hat note vel\n"
      "section main chords+arp^-12\n"
      "pattern chords 2x4 harm\n1:00 | D3 F4 A3\n2:0 | Bb2 D4 F3\n"
      "pattern arp 4 arp\n| t1+12:2 .2> t2\n2 | ~t3 .8 C4\n"
      "pattern fill 2 hat\nlane hat 9x\n"
      "order main^2+fill@1:2 |: main+fill@3*2\n"};
  const char alphabet[] = "|#.-~x*+:0129 \nCsbvAoffpatternrackorder=^@>tlnesci";
  std::uint32_t rng = 0x2545F491u;
  auto next = [&] {
    rng ^= rng << 13;
    rng ^= rng >> 17;
    rng ^= rng << 5;
    return rng;
  };
  for (const std::string& song : songs) {
    REQUIRE(parse_trk(song).ok());
    for (int i = 0; i < 4000; ++i) {
      std::string m = song;
      const int edits = 1 + static_cast<int>(next() % 4);
      for (int e = 0; e < edits; ++e) {
        const size_t pos = next() % (m.size() + 1);
        const char c = alphabet[next() % (sizeof(alphabet) - 1)];
        switch (next() % 3) {
          case 0: if (pos < m.size()) m[pos] = c; break;
          case 1: m.insert(pos, 1, c); break;
          default: if (pos < m.size()) m.erase(pos, 1); break;
        }
      }
      auto r = parse_trk(m);
      if (r.ok()) {
        CHECK(r.song.length() > 0);
        CHECK(r.song.rows.size() == static_cast<size_t>(r.song.length() * r.song.tracks));
        for (const TrkRow& row : r.song.rows)
          REQUIRE((std::isfinite(row.voct) && std::isfinite(row.cva) && std::isfinite(row.cvb)));
      } else {
        REQUIRE(r.errors.size() == 1);
        CHECK(r.errors[0].line >= 1);
        CHECK(r.errors[0].col >= 1);
      }
    }
  }
}

TEST_CASE("trk: hostile bytes and sizes") {
  CHECK_FALSE(parse_trk("").ok());
  CHECK_FALSE(parse_trk(std::string("\0\xff\x01|", 4)).ok());
  CHECK_FALSE(parse_trk(std::string(100000, '|')).ok());
  CHECK_FALSE(parse_trk("track a cva\npattern p 1\n| 1e400\norder p\n").ok());
  CHECK_FALSE(parse_trk("track a cva\npattern p 1\n| nan\norder p\n").ok());
  CHECK_FALSE(parse_trk("track a cva\npattern p 99999999999\n").ok());
  CHECK_FALSE(parse_trk("track a cva\npattern p 1\norder p*99999999999\n").ok());
  // A single huge line of empty cells is fine within one pattern width.
  CHECK(parse_trk("track a cva\npattern p 1\n|" + std::string(10000, '.') + "\norder p\n").ok());
}
