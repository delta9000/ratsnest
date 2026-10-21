#pragma once
#include <functional>
#include <string>
#include <vector>
#include "dsl/ast.h"
#include "dsl/diag.h"
#include "graph/graph.h"

namespace ms::graph {

using FileReader = std::function<bool(const std::string& path, std::string& out)>;

bool read_text_file(const std::string& path, std::string& out);

struct SubExpansion {
  std::vector<std::string> files;
};

SubExpansion expand_subs(dsl::PatchAst& ast, const std::string& patch_dir,
                         const FileReader& read, Diags& errors);

// Reads each `tracker`'s `file:` (resolved against the declaring file's
// directory, like a sub), reports .trk errors as file:line:col, and embeds
// the song as config `text`. Every path read is appended to `files` so the
// watcher hot-reloads .trk edits. See graph/trackfile.cpp.
void embed_tracker_files(dsl::PatchAst& ast, const std::string& patch_dir,
                         const FileReader& read, std::vector<std::string>& files,
                         Diags& errors);

// Patch text to graph: parse, expand subpatches against the patch file's
// directory, compile. `ast` keeps the expanded patch for a second compile;
// `files` lists the subpatch files read. Errors are in compiled.errors.
struct PatchBuild {
  dsl::PatchAst ast;
  std::vector<std::string> files;
  CompileResult compiled;
};
PatchBuild build_patch(std::string_view src, const std::string& patch_path, double rate);

}  // namespace ms::graph
