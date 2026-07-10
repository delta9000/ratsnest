#include "dsl/lexer.h"
#include <cctype>
#include <cstdlib>

namespace ms::dsl {
namespace {

bool ident_start(char c) { return std::islower(static_cast<unsigned char>(c)) || c == '_'; }
bool ident_char(char c) { return ident_start(c) || std::isdigit(static_cast<unsigned char>(c)); }

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
    r.errors.push_back(Diag{l, c, std::move(msg)});
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
      default: err(tl, tc, std::string("unexpected character '") + c + "'"); break;
    }
    advance(1);
  }
  push(Tok::End, "", 0, line, col);
  return r;
}
}  // namespace ms::dsl
