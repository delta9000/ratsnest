#pragma once
#include <string>

namespace ms::dsl {
enum class Tok {
  Ident, Number, Str,
  LBrace, RBrace, LBracket, RBracket,
  Colon, Comma, Equals, Arrow, Dot,
  Newline, End,
};

struct Token {
  Tok kind;
  std::string text;
  double num = 0.0;
  int line = 1;
  int col = 1;
};
}  // namespace ms::dsl
