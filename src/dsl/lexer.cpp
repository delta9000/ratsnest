#include "dsl/lexer.h"
#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <string>
#include <string_view>

namespace ms::dsl {
namespace {

bool is_digit(char c) { return std::isdigit(static_cast<unsigned char>(c)) != 0; }
bool is_space(char c) { return std::isspace(static_cast<unsigned char>(c)) != 0; }
bool ident_start(char c) { return std::islower(static_cast<unsigned char>(c)) || c == '_'; }
bool ident_char(char c) { return ident_start(c) || is_digit(c); }
// Wider than an identifier: lets a diagnostic quote a whole mistyped word
// (`Hz`, `Osc`) instead of stopping at its first uppercase letter.
bool word_char(char c) { return std::isalnum(static_cast<unsigned char>(c)) || c == '_'; }

}  // namespace

// A note-name literal: uppercase letter A-G, optional accidental (s = sharp,
// b = flat — '#' is unavailable, it starts a comment), one octave digit 0-9,
// and an optional trailing 'v'. Scientific pitch notation (C4 = middle C,
// A4 = 440 Hz) -> a number. Bare (`A4`) yields frequency in Hz; the 'v'
// suffix (`A4v`) yields v/oct — octaves from A3 = 0.0, so pitch composes under
// summed modulation. Uppercase letters are otherwise unused in the grammar, so
// this is unambiguous with identifiers (which are lowercase).
bool note_num(std::string_view s, double& out) {
  const bool voct = !s.empty() && s.back() == 'v';
  if (voct) s.remove_suffix(1);
  if (s.size() < 2 || s.size() > 3 || !is_digit(s.back())) return false;
  // Semitones above C, found by the letter's position; gaps are not notes.
  constexpr std::string_view kLetters = "C?D?EF?G?A?B";
  const auto letter = kLetters.find(s[0]);
  if (s[0] == '?' || letter == std::string_view::npos) return false;
  int pc = static_cast<int>(letter);
  if (s.size() == 3) {
    if (s[1] != 's' && s[1] != 'b') return false;
    pc += s[1] == 's' ? 1 : -1;
  }
  const int midi = (s.back() - '0' + 1) * 12 + pc;  // scientific: C4 -> MIDI 60
  out = voct ? static_cast<double>(midi - 57) / 12.0            // A3 = 0.0
             : 440.0 * std::exp2(static_cast<double>(midi - 69) / 12.0);
  return true;
}

namespace {

// A stray byte, made safe to print inside a one-line diagnostic: control
// bytes and lone high bytes (a fragment of UTF-8) become \xNN.
std::string printable(char c) {
  const auto u = static_cast<unsigned char>(c);
  if (u >= 0x20 && u < 0x7f) return std::string(1, c);
  char buf[8];
  std::snprintf(buf, sizeof buf, "\\x%02x", u);
  return buf;
}

std::string lower(std::string s) {
  std::transform(s.begin(), s.end(), s.begin(),
                 [](char c) { return static_cast<char>(std::tolower(static_cast<unsigned char>(c))); });
  return s;
}

bool unit_factor(std::string_view u, double& factor) {
  if (u == "hz" || u == "s" || u == "bpm") { factor = 1.0; return true; }
  if (u == "khz") { factor = 1000.0; return true; }
  if (u == "ms") { factor = 0.001; return true; }
  return false;
}

// Single-character tokens; `nest` tracks {} and [] depth, inside which
// newlines are not statement breaks.
struct Punct { char c; Tok kind; int nest; };
constexpr Punct kPunct[] = {
    {'{', Tok::LBrace, 1},   {'}', Tok::RBrace, -1}, {'[', Tok::LBracket, 1},
    {']', Tok::RBracket, -1}, {':', Tok::Colon, 0},  {',', Tok::Comma, 0},
    {'=', Tok::Equals, 0},   {'.', Tok::Dot, 0},
};

class Lexer {
 public:
  explicit Lexer(std::string_view src) : src_(src) {}

  LexResult run() {
    while (i_ < src_.size()) step();
    push(Tok::End, "", 0, line_, col_);
    return std::move(r_);
  }

 private:
  // The byte k ahead of the cursor, or '\0' past the end (which no rule
  // below accepts, so it reads as "no such character").
  char at(size_t k = 0) const { return i_ + k < src_.size() ? src_[i_ + k] : '\0'; }

  void push(Tok k, std::string text, double num, int l, int c) {
    r_.tokens.push_back(Token{k, std::move(text), num, l, c});
  }
  void err(int l, int c, std::string msg) {
    r_.errors.push_back(Diag{l, c, std::move(msg), {}});
  }
  void advance(size_t n) {
    for (size_t k = 0; k < n && i_ < src_.size(); ++k, ++i_) {
      if (src_[i_] == '\n') { ++line_; col_ = 1; } else { ++col_; }
    }
  }
  // A digit, or '.' then a digit, k bytes ahead.
  bool number_at(size_t k) const {
    return is_digit(at(k)) || (at(k) == '.' && is_digit(at(k + 1)));
  }

  template <class Pred>
  std::string_view take_while(Pred pred) {
    const size_t start = i_;
    while (i_ < src_.size() && pred(src_[i_])) advance(1);
    return src_.substr(start, i_ - start);
  }

  void step() {
    if (skip_trivia()) return;
    const int tl = line_, tc = col_;
    if (at() == '-' && at(1) == '>') { push(Tok::Arrow, "->", 0, tl, tc); advance(2); return; }
    // A '-' directly before a number negates it.
    const bool neg = at() == '-' && number_at(1);
    if (neg) advance(1);
    const char c = at();
    if (number_at(0)) return number(neg, tl, tc);
    if (ident_start(c)) {
      push(Tok::Ident, std::string(take_while(ident_char)), 0, tl, tc);
      return;
    }
    if (c >= 'A' && c <= 'Z') return note(tl, tc);
    if (c == '"') return quoted(tl, tc);
    punct(c, tl, tc);
  }

  // Comments, whitespace, and newlines (a token only outside {} and []).
  bool skip_trivia() {
    const char c = at();
    if (c == '#') {
      take_while([](char ch) { return ch != '\n'; });
    } else if (c == '\n') {
      advance(1);
      if (depth_ == 0) push(Tok::Newline, "\n", 0, line_, col_);
    } else if (is_space(c)) {
      advance(1);
    } else {
      return false;
    }
    return true;
  }

  void number(bool neg, int tl, int tc) {
    const std::string_view digits = take_while([](char ch) { return is_digit(ch) || ch == '.'; });
    const double v = std::strtod(std::string(digits).c_str(), nullptr);
    const std::string unit(take_while(word_char));
    double factor = 1.0;
    if (!unit.empty() && !unit_factor(unit, factor))
      err(tl, tc, "unknown unit '" + unit + "'" +
                      (unit_factor(lower(unit), factor) ? " (units are lowercase: " + lower(unit) + ")"
                                                         : " (hz, khz, ms, s, bpm)"));
    push(Tok::Number, "", (neg ? -v : v) * factor, tl, tc);
  }

  // Note-name literal (A4, Cs4, Bb2). Uppercase letters are otherwise unused,
  // so consume the run and validate it as a note -> a Number token in Hz.
  // `C#4` is consumed whole: '#' would otherwise comment out the line's rest.
  void note(int tl, int tc) {
    const size_t start = i_;
    advance(1);
    const bool hash = at() == '#';
    advance(hash);
    take_while(word_char);
    const std::string_view tok = src_.substr(start, i_ - start);
    double val = 0.0;
    if (!hash && note_num(tok, val)) return push(Tok::Number, "", val, tl, tc);
    err(tl, tc, "'" + std::string(tok) + "' is not a note name (A4; Cs4 for sharp -- '#' starts"
                " a comment; Bb2 for flat; A4v for v/oct) nor a name (names are lowercase)");
    // Recover as the name it probably meant, so one typo reports once.
    push(Tok::Ident, std::string(tok), 0, tl, tc);
  }

  void quoted(int tl, int tc) {
    advance(1);
    const std::string_view body = take_while([](char ch) { return ch != '"' && ch != '\n'; });
    if (at() != '"') {
      err(tl, tc, "unterminated string");
      return;
    }
    push(Tok::Str, std::string(body), 0, tl, tc);
    advance(1);
  }

  void punct(char c, int tl, int tc) {
    if (c == '=' && at(1) == '>') {  // read as the arrow it was meant to be
      err(tl, tc, "'=>' is not an arrow; cables use '->'");
      push(Tok::Arrow, "->", 0, tl, tc);
      advance(2);
      return;
    }
    const auto* p = std::find_if(std::begin(kPunct), std::end(kPunct),
                                 [c](const Punct& q) { return q.c == c; });
    if (p == std::end(kPunct)) {
      err(tl, tc, "unexpected character '" + printable(c) + "'" +
                      (c == '-' ? " (cables use '->'; names use '_', not '-')" : ""));
    } else {
      depth_ = std::max(0, depth_ + p->nest);
      push(p->kind, std::string(1, c), 0, tl, tc);
    }
    advance(1);
  }

  std::string_view src_;
  size_t i_ = 0;
  int line_ = 1, col_ = 1;
  int depth_ = 0;
  LexResult r_;
};

}  // namespace

LexResult lex(std::string_view src) { return Lexer(src).run(); }

}  // namespace ms::dsl
