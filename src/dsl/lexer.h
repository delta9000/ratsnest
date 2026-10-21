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
// A note-name literal (`A4`, `Cs4`, `Bb2`, `A4v`) -> Hz, or v/oct from
// A3 = 0 with the trailing `v`. False if `s` is not one. Shared with .trk.
bool note_num(std::string_view s, double& out);
}  // namespace ms::dsl
