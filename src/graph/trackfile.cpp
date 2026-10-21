// The tracker's file loader: a non-RT build pass after subpatch expansion.
// See docs/superpowers/specs/2026-10-21-tracker-design.md §5.
#include <algorithm>
#include <filesystem>
#include "graph/subs.h"
#include "io/trk.h"

namespace ms::graph {
namespace {

void set_text(dsl::ModuleDecl& m, const dsl::ParamDecl& at, std::string text) {
  auto it = std::find_if(m.params.begin(), m.params.end(),
                         [](const dsl::ParamDecl& p) { return p.name == "text"; });
  dsl::ParamDecl p{"text", {dsl::Value::Kind::Str, 0.0, std::move(text), {}}, at.line, at.col};
  if (it == m.params.end()) m.params.push_back(std::move(p));
  else *it = std::move(p);
}

void embed(dsl::ModuleDecl& m, const dsl::ParamDecl& pf, const std::string& patch_dir,
           const FileReader& read, std::vector<std::string>& files, Diags& errors) {
  const std::filesystem::path dir =
      m.file.empty() ? std::filesystem::path(patch_dir)
                     : std::filesystem::path(m.file).parent_path();
  const std::string path = (dir / pf.value.str).lexically_normal().string();
  std::string text;
  if (!read(path, text)) {
    errors.push_back({pf.line, pf.col, "cannot read tracker file '" + path + "'", m.file});
    return;
  }
  if (std::find(files.begin(), files.end(), path) == files.end()) files.push_back(path);
  io::TrkParse song = io::parse_trk(text);
  for (Diag d : song.errors) {
    d.file = path;
    errors.push_back(std::move(d));
  }
  if (song.ok()) set_text(m, pf, std::move(text));
}

}  // namespace

void embed_tracker_files(dsl::PatchAst& ast, const std::string& patch_dir,
                         const FileReader& read, std::vector<std::string>& files,
                         Diags& errors) {
  for (auto& m : ast.modules) {
    if (m.type != "tracker") continue;
    auto pf = std::find_if(m.params.begin(), m.params.end(),
                           [](const dsl::ParamDecl& p) { return p.name == "file"; });
    if (pf == m.params.end() || pf->value.str.empty()) continue;
    const dsl::ParamDecl file = *pf;  // set_text may grow params
    embed(m, file, patch_dir, read, files, errors);
  }
}

}  // namespace ms::graph
