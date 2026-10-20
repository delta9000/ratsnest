#include "dsl/lexer.h"
#include <cctype>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <string>
#include <string_view>

namespace ms::dsl {
namespace {

bool ident_start(char c) { return std::islower(static_cast<unsigned char>(c)) || c == '_'; }
bool ident_char(char c) { return ident_start(c) || std::isdigit(static_cast<unsigned char>(c)); }

// A note-name literal: uppercase letter A-G, optional accidental (s = sharp,
// b = flat — '#' is unavailable, it starts a comment), one octave digit 0-9,
// and an optional trailing 'v'. Scientific pitch notation (C4 = middle C,
// A4 = 440 Hz) -> a number. Bare (`A4`) yields frequency in Hz; the 'v'
// suffix (`A4v`) yields v/oct — octaves from A3 = 0.0, so pitch composes under
// summed modulation. Uppercase letters are otherwise unused in the grammar, so
// this is unambiguous with identifiers (which are lowercase).
bool note_num(std::string_view s, double& out) {
  bool voct = false;
  if (!s.empty() && s.back() == 'v') { voct = true; s.remove_suffix(1); }
  if (s.size() < 2 || s.size() > 3) return false;
  int pc = 0;
  switch (s[0]) {
    case 'C': pc = 0;  break; case 'D': pc = 2;  break; case 'E': pc = 4;  break;
    case 'F': pc = 5;  break; case 'G': pc = 7;  break; case 'A': pc = 9;  break;
    case 'B': pc = 11; break; default: return false;
  }
  std::size_t idx = 1;
  if (s[idx] == 's') { pc += 1; ++idx; }
  else if (s[idx] == 'b') { pc -= 1; ++idx; }
  if (idx != s.size() - 1) return false;
  const char d = s[idx];
  if (d < '0' || d > '9') return false;
  const int midi = (d - '0' + 1) * 12 + pc;  // scientific: C4 -> MIDI 60
  out = voct ? static_cast<double>(midi - 57) / 12.0            // A3 = 0.0
             : 440.0 * std::exp2(static_cast<double>(midi - 69) / 12.0);
  return true;
}

// A stray byte, made safe to print inside a one-line diagnostic: control
// bytes and lone high bytes (a fragment of UTF-8) become \xNN.
std::string printable(char c) {
  const auto u = static_cast<unsigned char>(c);
  if (u >= 0x20 && u < 0x7f) return std::string(1, c);
  char buf[8];
  std::snprintf(buf, sizeof buf, "\\x%02x", u);
  return buf;
}

bool unit_factor(std::string_view u, double& factor) {
  if (u == "hz" || u == "s" || u == "bpm") { factor = 1.0; return true; }
  if (u == "khz") { factor = 1000.0; return true; }
  if (u == "ms") { factor = 0.001; return true; }
  return false;
}

}  // namespace

LexResult lex(std::string_view src) {
  LexResult r;
  size_t i = 0;
  int line = 1, col = 1;
  int depth = 0;  // {} and [] nesting; newlines suppressed inside

  auto push = [&](Tok k, std::string text, double num, int l, int c) {
    r.tokens.push_back(Token{k, std::move(text), num, l, c});
  };
  auto err = [&](int l, int c, std::string msg) {
    r.errors.push_back(Diag{l, c, std::move(msg), {}});
  };
  auto advance = [&](size_t n) {
    for (size_t k = 0; k < n && i < src.size(); ++k, ++i) {
      if (src[i] == '\n') { ++line; col = 1; } else { ++col; }
    }
  };

  while (i < src.size()) {
    char c = src[i];
    int tl = line, tc = col;
    if (c == '#') {
      while (i < src.size() && src[i] != '\n') advance(1);
      continue;
    }
    if (c == '\n') {
      advance(1);
      if (depth == 0) push(Tok::Newline, "\n", 0, line, col);
      continue;
    }
    if (std::isspace(static_cast<unsigned char>(c))) { advance(1); continue; }

    if (c == '-' && i + 1 < src.size() && src[i + 1] == '>') {
      push(Tok::Arrow, "->", 0, tl, tc); advance(2); continue;
    }

    bool neg = false;
    if (c == '-' && i + 1 < src.size() &&
        (std::isdigit(static_cast<unsigned char>(src[i + 1])) || src[i + 1] == '.')) {
      neg = true; advance(1); c = src[i];
    }
    if (std::isdigit(static_cast<unsigned char>(c)) || (c == '.' && i + 1 < src.size() &&
        std::isdigit(static_cast<unsigned char>(src[i + 1])))) {
      size_t start = i;
      while (i < src.size() && (std::isdigit(static_cast<unsigned char>(src[i])) || src[i] == '.'))
        advance(1);
      double v = std::strtod(std::string(src.substr(start, i - start)).c_str(), nullptr);
      size_t ustart = i;
      while (i < src.size() && ident_char(src[i])) advance(1);
      std::string_view unit = src.substr(ustart, i - ustart);
      double factor = 1.0;
      if (!unit.empty() && !unit_factor(unit, factor)) {
        err(tl, tc, "unknown unit '" + std::string(unit) + "' (hz, khz, ms, s, bpm)");
      }
      push(Tok::Number, "", (neg ? -v : v) * factor, tl, tc);
      continue;
    }

    if (ident_start(c)) {
      size_t start = i;
      while (i < src.size() && ident_char(src[i])) advance(1);
      push(Tok::Ident, std::string(src.substr(start, i - start)), 0, tl, tc);
      continue;
    }

    // Note-name literal (A4, Cs4, Bb2). Uppercase A-G is otherwise unused, so
    // consume the run and validate it as a note -> a Number token in Hz.
    if (c >= 'A' && c <= 'G') {
      size_t start = i;
      advance(1);
      while (i < src.size() && ident_char(src[i])) advance(1);
      std::string_view tok = src.substr(start, i - start);
      double val = 0.0;
      if (note_num(tok, val)) {
        push(Tok::Number, "", val, tl, tc);
      } else {
        err(tl, tc, "invalid note name '" + std::string(tok) +
                        "' (use e.g. A4, Cs4 for sharp, Bb2 for flat, A4v for v/oct)");
      }
      continue;
    }

    if (c == '"') {
      advance(1);
      size_t start = i;
      while (i < src.size() && src[i] != '"' && src[i] != '\n') advance(1);
      if (i >= src.size() || src[i] != '"') {
        err(tl, tc, "unterminated string");
      } else {
        push(Tok::Str, std::string(src.substr(start, i - start)), 0, tl, tc);
        advance(1);
      }
      continue;
    }

    switch (c) {
      case '{': ++depth; push(Tok::LBrace, "{", 0, tl, tc); break;
      case '}': if (depth > 0) --depth; push(Tok::RBrace, "}", 0, tl, tc); break;
      case '[': ++depth; push(Tok::LBracket, "[", 0, tl, tc); break;
      case ']': if (depth > 0) --depth; push(Tok::RBracket, "]", 0, tl, tc); break;
      case ':': push(Tok::Colon, ":", 0, tl, tc); break;
      case ',': push(Tok::Comma, ",", 0, tl, tc); break;
      case '=': push(Tok::Equals, "=", 0, tl, tc); break;
      case '.': push(Tok::Dot, ".", 0, tl, tc); break;
      default: err(tl, tc, "unexpected character '" + printable(c) + "'"); break;
    }
    advance(1);
  }
  push(Tok::End, "", 0, line, col);
  return r;
}
}  // namespace ms::dsl
