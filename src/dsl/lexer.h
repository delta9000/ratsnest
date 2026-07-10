#pragma once
#include <string_view>
#include <vector>
#include "dsl/diag.h"
#include "dsl/token.h"

namespace ms::dsl {
struct LexResult {
  std::vector<Token> tokens;
  Diags errors;
};
LexResult lex(std::string_view src);
}  // namespace ms::dsl
