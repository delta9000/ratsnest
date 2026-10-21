// The .trk song format: every syntax element, every diagnostic with
// line:col, and mutation fuzzing (rule 6: report, never crash or hang).
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
  fails(h + "pattern p 4\npattern p 4\n", 3, 9, "duplicate pattern 'p'");
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

TEST_CASE("trk: the longest song fits exactly") {
  auto s = ok("track a cva\npattern p 1024\norder p*64\n");
  CHECK(s.length() == 65536);
}

// Byte-level mutations of a realistic song: every result either parses or
// reports exactly one positioned error, and never crashes or hangs (the
// sanitizer build turns any memory error into a failure).
TEST_CASE("trk: mutation fuzz never crashes and always positions its error") {
  const std::string song =
      "# fuzz seed\ntrack kick note\ntrack bass note vel cva\ntrack harm note cva cvb\n"
      "pattern beat 4 kick\n00 | x\n02 | x\n"
      "pattern verse 8 bass harm\n00 | D2 .9 1 | D3 F4 A3\n| ~E2\n04 | off | Bb2 D4 F3\n"
      "order verse*2 |: verse+beat*3\n";
  REQUIRE(parse_trk(song).ok());
  const char alphabet[] = "|#.-~x*+:0129 \nCsbvAoffpatternrackorder=";
  std::uint32_t rng = 0x2545F491u;
  auto next = [&] {
    rng ^= rng << 13;
    rng ^= rng >> 17;
    rng ^= rng << 5;
    return rng;
  };
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
    } else {
      REQUIRE(r.errors.size() == 1);
      CHECK(r.errors[0].line >= 1);
      CHECK(r.errors[0].col >= 1);
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
