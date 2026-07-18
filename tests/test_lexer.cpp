#include <doctest/doctest.h>
#include "dsl/lexer.h"

using namespace ms::dsl;

static std::vector<Tok> kinds(const LexResult& r) {
  std::vector<Tok> k;
  for (auto& t : r.tokens) k.push_back(t.kind);
  return k;
}

TEST_CASE("lexes a declaration") {
  auto r = lex("osc1 = vco { freq: 110hz, wave: saw }");
  REQUIRE(r.errors.empty());
  CHECK(kinds(r) == std::vector<Tok>{Tok::Ident, Tok::Equals, Tok::Ident,
      Tok::LBrace, Tok::Ident, Tok::Colon, Tok::Number, Tok::Comma,
      Tok::Ident, Tok::Colon, Tok::Ident, Tok::RBrace, Tok::End});
  CHECK(r.tokens[6].num == doctest::Approx(110.0));
}

TEST_CASE("normalizes units") {
  auto r = lex("1.2khz 250ms 2s 0.5 120bpm");
  REQUIRE(r.errors.empty());
  CHECK(r.tokens[0].num == doctest::Approx(1200.0));
  CHECK(r.tokens[1].num == doctest::Approx(0.25));
  CHECK(r.tokens[2].num == doctest::Approx(2.0));
  CHECK(r.tokens[3].num == doctest::Approx(0.5));
  CHECK(r.tokens[4].num == doctest::Approx(120.0));
}

TEST_CASE("arrow, dot, depth, negative numbers") {
  auto r = lex("lfo1 -> filt.cutoff : -0.5");
  REQUIRE(r.errors.empty());
  CHECK(kinds(r) == std::vector<Tok>{Tok::Ident, Tok::Arrow, Tok::Ident,
      Tok::Dot, Tok::Ident, Tok::Colon, Tok::Number, Tok::End});
  CHECK(r.tokens[6].num == doctest::Approx(-0.5));
}

TEST_CASE("comments and newlines; newlines suppressed inside brackets") {
  auto r = lex("a = vca # panel\n[1,\n2]\nb = vca");
  REQUIRE(r.errors.empty());
  int newlines = 0;
  for (auto& t : r.tokens) if (t.kind == Tok::Newline) ++newlines;
  CHECK(newlines == 2);  // after 'vca' and after ']', none inside [...]
}

TEST_CASE("strings") {
  auto r = lex("file: \"reel1.wav\"");
  REQUIRE(r.errors.empty());
  CHECK(r.tokens[2].kind == Tok::Str);
  CHECK(r.tokens[2].text == "reel1.wav");
}

TEST_CASE("unknown unit is an error with position") {
  auto r = lex("freq: 100furlongs");
  REQUIRE(r.errors.size() == 1);
  CHECK(r.errors[0].line == 1);
  CHECK(r.errors[0].msg.find("unknown unit") != std::string::npos);
}

TEST_CASE("tracks line and column") {
  auto r = lex("a = vca\nbb = vca");
  CHECK(r.tokens[3].line == 2);
  CHECK(r.tokens[3].col == 1);
}

TEST_CASE("note names lower to Hz; the 'v' suffix lowers to v/oct") {
  // Bare note = Hz (unchanged).
  auto hz = lex("A4 C4 A1");
  REQUIRE(hz.errors.empty());
  CHECK(hz.tokens[0].num == doctest::Approx(440.0));
  CHECK(hz.tokens[1].num == doctest::Approx(261.6256).epsilon(1e-4));
  CHECK(hz.tokens[2].num == doctest::Approx(55.0).epsilon(1e-4));

  // 'v' suffix = v/oct, octaves from A3 = 0.0 (voct = (midi - 57) / 12).
  auto v = lex("A3v A4v C4v Cs4v Bb2v");
  REQUIRE(v.errors.empty());
  CHECK(v.tokens[0].num == doctest::Approx(0.0));            // A3  midi 57
  CHECK(v.tokens[1].num == doctest::Approx(1.0));            // A4  midi 69
  CHECK(v.tokens[2].num == doctest::Approx(0.25));           // C4  midi 60 -> 3/12
  CHECK(v.tokens[3].num == doctest::Approx(4.0 / 12.0));     // Cs4 midi 61
  CHECK(v.tokens[4].num == doctest::Approx(-11.0 / 12.0));   // Bb2 midi 46
  // Db4v is the enharmonic of Cs4v.
  auto en = lex("Db4v");
  REQUIRE(en.errors.empty());
  CHECK(en.tokens[0].num == doctest::Approx(4.0 / 12.0));

  // A trailing letter that isn't the v/oct marker is still an error.
  auto bad = lex("A4x");
  REQUIRE(bad.errors.size() == 1);
  CHECK(bad.errors[0].msg.find("note name") != std::string::npos);
}
