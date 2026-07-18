#pragma once
#include <functional>
#include <string>
#include <vector>
#include "dsl/ast.h"
#include "dsl/diag.h"

namespace ms::graph {

using FileReader = std::function<bool(const std::string& path, std::string& out)>;

bool read_text_file(const std::string& path, std::string& out);

struct SubExpansion {
  std::vector<std::string> files;
};

SubExpansion expand_subs(dsl::PatchAst& ast, const std::string& patch_dir,
                         const FileReader& read, Diags& errors);

}  // namespace ms::graph
