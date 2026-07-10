#pragma once
#include <string_view>
#include "dsl/ast.h"
#include "dsl/diag.h"

namespace ms::dsl {
struct ParseResult {
  PatchAst ast;
  Diags errors;
  bool ok() const { return errors.empty(); }
};
ParseResult parse(std::string_view src);
}  // namespace ms::dsl
