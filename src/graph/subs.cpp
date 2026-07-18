#include "graph/subs.h"

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

#include "dsl/parser.h"

namespace ms::graph {

bool read_text_file(const std::string& path, std::string& out) {
  std::ifstream f(path);
  if (!f) return false;
  std::ostringstream ss;
  ss << f.rdbuf();
  out = ss.str();
  return true;
}

namespace {

constexpr size_t kMaxDepth = 16;

bool is_iface(const std::string& t) {
  return t == "param" || t == "input" || t == "output";
}

bool chain_under(const std::string& chain, const std::string& name) {
  return chain.size() > name.size() + 1 &&
         chain.compare(0, name.size(), name) == 0 && chain[name.size()] == '.';
}

const dsl::ParamDecl* find_param(const dsl::ModuleDecl& m, const std::string& n) {
  for (const auto& p : m.params)
    if (p.name == n) return &p;
  return nullptr;
}

using ParamMap = std::unordered_map<std::string, dsl::Value>;

struct Ctx {
  const FileReader& read;
  Diags& errors;
  size_t err0 = 0;
  std::vector<std::string> files;
  std::unordered_set<std::string> file_set;
  std::unordered_map<std::string, dsl::PatchAst> cache;
  std::vector<std::string> labels;
  std::unordered_set<std::string> stack;

  bool failed() const { return errors.size() != err0; }

  std::string chain() const {
    if (labels.empty()) return "";
    std::string s = " (via ";
    for (size_t i = 0; i < labels.size(); ++i) {
      if (i) s += " -> ";
      s += labels[i];
    }
    return s + ")";
  }
  void err(const std::string& file, int line, int col, std::string msg) {
    errors.push_back({line, col, std::move(msg) + chain(), file});
  }
};

void subst_value(const ParamMap& params, dsl::Value& v) {
  if (v.kind == dsl::Value::Kind::Ident) {
    auto it = params.find(v.str);
    if (it != params.end()) v = it->second;
  } else if (v.kind == dsl::Value::Kind::Array) {
    for (auto& item : v.items) subst_value(params, item);
  }
}

void substitute(Ctx& ctx, const std::string& file, dsl::PatchAst& body,
                const ParamMap& params) {
  for (auto& m : body.modules)
    for (auto& pd : m.params) {
      if (m.type == "sub" && (pd.name == "file" || pd.name == "def")) continue;
      subst_value(params, pd.value);
    }
  for (auto& c : body.cables) {
    if (!c.src.is_literal) {
      auto it = params.find(c.src.module);
      if (it != params.end()) {
        if (it->second.kind != dsl::Value::Kind::Num) {
          ctx.err(file, c.src.line, c.src.col,
                  "param '" + c.src.module + "' used as a cable source must be a number");
        } else {
          c.src.is_literal = true;
          c.src.literal = it->second.num;
          c.src.module.clear();
        }
      }
    }
    if (!c.depth_ident.empty()) {
      auto it = params.find(c.depth_ident);
      if (it != params.end()) {
        if (it->second.kind != dsl::Value::Kind::Num) {
          ctx.err(file, c.line, c.col,
                  "param '" + c.depth_ident + "' used as a cable depth must be a number");
        } else {
          c.depth = it->second.num;
          c.depth_ident.clear();
        }
      }
    }
  }
}

struct Interface {
  ParamMap params;
  std::vector<std::pair<std::string, double>> inputs;
  std::vector<std::string> outputs;
  bool any() const { return !params.empty() || !inputs.empty() || !outputs.empty(); }
  const double* input_def(const std::string& n) const {
    for (const auto& [name, d] : inputs)
      if (name == n) return &d;
    return nullptr;
  }
  bool is_output(const std::string& n) const {
    return std::find(outputs.begin(), outputs.end(), n) != outputs.end();
  }
};

Interface scan_interface(Ctx& ctx, const std::string& file, const dsl::PatchAst& body) {
  Interface f;
  for (const auto& m : body.modules) {
    if (m.type == "param") {
      const dsl::ParamDecl* d = find_param(m, "default");
      if (!d) {
        ctx.err(file, m.line, m.col, "param '" + m.name + "' requires 'default:'");
        continue;
      }
      f.params.emplace(m.name, d->value);
    } else if (m.type == "input") {
      double def = 0.0;
      if (const dsl::ParamDecl* d = find_param(m, "default")) {
        if (d->value.kind != dsl::Value::Kind::Num)
          ctx.err(file, d->line, d->col, "input '" + m.name + "' default must be a number");
        else
          def = d->value.num;
      }
      f.inputs.emplace_back(m.name, def);
    } else if (m.type == "output") {
      f.outputs.push_back(m.name);
    }
  }
  return f;
}

void splice_ports(Ctx& ctx, const std::string& file, dsl::PatchAst& parent,
                  const std::string& inst, dsl::PatchAst& body, const Interface& f) {
  auto ref = [&](const std::string& port) {
    return inst.empty() ? port : inst + "." + port;
  };
  auto is_input = [&](const std::string& n) { return f.input_def(n) != nullptr; };

  std::unordered_map<std::string, std::vector<dsl::CableDecl>> in_targets;
  std::unordered_map<std::string, std::vector<dsl::CableDecl>> out_sources;
  {
    std::vector<dsl::CableDecl> kept;
    for (auto& c : body.cables) {
      const bool src_is_input = !c.src.is_literal && is_input(c.src.module);
      std::string out_name;
      if (f.is_output(c.dst.module)) out_name = c.dst.module;
      else if (c.dst.module.size() > 3 && c.dst.module.rfind(".in") == c.dst.module.size() - 3) {
        std::string head = c.dst.module.substr(0, c.dst.module.size() - 3);
        if (f.is_output(head)) out_name = head;
      }
      if (src_is_input && !out_name.empty()) {
        ctx.err(file, c.line, c.col,
                "cable may not connect input '" + c.src.module +
                "' directly to output '" + out_name + "'");
        return;
      }
      if (src_is_input) in_targets[c.src.module].push_back(c);
      else if (!out_name.empty()) out_sources[out_name].push_back(c);
      else kept.push_back(std::move(c));
    }
    body.cables = std::move(kept);
  }
  for (const auto& c : body.cables)
    if (!c.src.is_literal && f.is_output(c.src.module) && !inst.empty())
      ctx.err(file, c.src.line, c.src.col,
              "output '" + c.src.module + "' cannot be read inside its own subpatch");
  if (ctx.failed()) return;

  auto stamp = [&](const std::string& chain) {
    return inst.empty() ? chain : inst + "." + chain;
  };
  std::vector<dsl::CableDecl> out_cables;
  std::unordered_set<std::string> fed_inputs;
  for (const auto& c : parent.cables) {
    std::vector<dsl::CableDecl> stage{c};
    if (!c.src.is_literal) {
      std::string out_name;
      if (!inst.empty() && c.src.module == inst) {
        if (f.outputs.empty()) {
          ctx.err(file, c.src.line, c.src.col,
                  "subpatch '" + inst + "' has no outputs");
          return;
        }
        out_name = f.outputs[0];
      } else {
        for (const auto& o : f.outputs)
          if (c.src.module == ref(o)) out_name = o;
      }
      for (const auto& i : f.inputs)
        if (c.src.module == ref(i.first)) {
          ctx.err(file, c.src.line, c.src.col,
                  "input '" + i.first + "' of '" + inst + "' cannot be a source");
          return;
        }
      if (!out_name.empty()) {
        stage.clear();
        for (const auto& s : out_sources[out_name]) {
          dsl::CableDecl nc = c;
          nc.src = s.src;
          if (!nc.src.is_literal) nc.src.module = stamp(s.src.module);
          nc.depth = c.depth * s.depth;
          stage.push_back(std::move(nc));
        }
      }
    }
    for (const auto& sc : stage) {
      std::string in_name;
      for (const auto& i : f.inputs)
        if (sc.dst.module == ref(i.first)) in_name = i.first;
      for (const auto& o : f.outputs) {
        if (sc.dst.module == ref(o)) {
          ctx.err(file, sc.dst.line, sc.dst.col,
                  "output '" + o + "' of '" + inst + "' cannot be a destination");
          return;
        }
        if (chain_under(sc.dst.module, ref(o))) {
          ctx.err(file, sc.dst.line, sc.dst.col, "port '" + o + "' has no members");
          return;
        }
      }
      for (const auto& i : f.inputs)
        if (chain_under(sc.dst.module, ref(i.first))) {
          ctx.err(file, sc.dst.line, sc.dst.col, "port '" + i.first + "' has no members");
          return;
        }
      if (in_name.empty()) {
        out_cables.push_back(sc);
        continue;
      }
      fed_inputs.insert(in_name);
      for (const auto& t : in_targets[in_name]) {
        dsl::CableDecl nc = sc;
        nc.dst = t.dst;
        nc.dst.module = stamp(t.dst.module);
        nc.depth = sc.depth * t.depth;
        out_cables.push_back(std::move(nc));
      }
    }
  }

  for (const auto& [name, def] : f.inputs) {
    if (fed_inputs.count(name) || def == 0.0) continue;
    for (const auto& t : in_targets[name]) {
      dsl::CableDecl nc = t;
      nc.src = {};
      nc.src.is_literal = true;
      nc.src.literal = def;
      nc.dst.module = stamp(t.dst.module);
      out_cables.push_back(std::move(nc));
    }
  }
  parent.cables = std::move(out_cables);

  std::vector<dsl::ModuleDecl> keep;
  for (auto& m : body.modules)
    if (!is_iface(m.type)) keep.push_back(std::move(m));
  body.modules = std::move(keep);
}

void expand_in(Ctx& ctx, dsl::PatchAst& ast, const std::string& dir,
               const std::string& file);

void do_body(Ctx& ctx, dsl::PatchAst& parent, size_t idx, const dsl::ModuleDecl& decl,
             const std::string& parent_file, dsl::PatchAst body,
             const std::string& body_dir, const std::string& body_file) {
  Interface f = scan_interface(ctx, body_file, body);
  if (ctx.failed()) return;

  ParamMap params = f.params;
  for (const auto& o : decl.params) {
    if (o.name == "file" || o.name == "def") continue;
    auto it = params.find(o.name);
    if (it == params.end())
      ctx.err(parent_file, o.line, o.col,
              "sub '" + decl.name + "' has no param '" + o.name + "'");
    else
      it->second = o.value;
  }
  if (ctx.failed()) return;

  substitute(ctx, body_file, body, params);
  if (ctx.failed()) return;

  expand_in(ctx, body, body_dir, body_file);
  if (ctx.failed()) return;

  splice_ports(ctx, body_file, parent, decl.name, body, f);
  if (ctx.failed()) return;

  for (auto& m : body.modules) m.name = decl.name + "." + m.name;
  for (auto& c : body.cables) {
    if (!c.src.is_literal) c.src.module = decl.name + "." + c.src.module;
    c.dst.module = decl.name + "." + c.dst.module;
  }
  parent.modules.erase(parent.modules.begin() + static_cast<long>(idx));
  parent.modules.insert(parent.modules.begin() + static_cast<long>(idx),
                        std::make_move_iterator(body.modules.begin()),
                        std::make_move_iterator(body.modules.end()));
  parent.cables.insert(parent.cables.end(),
                       std::make_move_iterator(body.cables.begin()),
                       std::make_move_iterator(body.cables.end()));
}

void instantiate(Ctx& ctx, dsl::PatchAst& parent, size_t idx,
                 const std::string& dir, const std::string& file) {
  const dsl::ModuleDecl decl = parent.modules[idx];
  const dsl::ParamDecl* pf = find_param(decl, "file");
  const dsl::ParamDecl* pd = find_param(decl, "def");
  if ((pf != nullptr) == (pd != nullptr)) {
    ctx.err(file, decl.line, decl.col,
            "sub '" + decl.name + "' requires exactly one of 'file:' or 'def:'");
    return;
  }
  if (pd) {
    ctx.err(file, pd->line, pd->col, "unknown subdef '" + pd->value.str + "'");
    return;
  }
  if (pf->value.kind != dsl::Value::Kind::Str) {
    ctx.err(file, pf->line, pf->col, "sub 'file:' expects a quoted path");
    return;
  }

  const std::filesystem::path p =
      (std::filesystem::path(dir) / pf->value.str).lexically_normal();
  const std::string path = p.string();
  const std::string key = "file:" + path;
  if (ctx.stack.count(key)) {
    ctx.err(file, decl.line, decl.col, "subpatch include cycle at '" + path + "'");
    return;
  }
  if (ctx.stack.size() >= kMaxDepth) {
    ctx.err(file, decl.line, decl.col, "subpatch nesting deeper than 16");
    return;
  }

  auto it = ctx.cache.find(path);
  if (it == ctx.cache.end()) {
    std::string src;
    if (!ctx.read(path, src)) {
      ctx.err(file, pf->line, pf->col, "cannot read subpatch '" + path + "'");
      return;
    }
    auto pr = dsl::parse(src);
    if (!pr.ok()) {
      for (const auto& d : pr.errors) ctx.err(path, d.line, d.col, d.msg);
      return;
    }
    it = ctx.cache.emplace(path, std::move(pr.ast)).first;
    if (ctx.file_set.insert(path).second) ctx.files.push_back(path);
  }

  ctx.stack.insert(key);
  ctx.labels.push_back(decl.name + " at " + (file.empty() ? "patch" : file) + ":" +
                       std::to_string(decl.line));
  do_body(ctx, parent, idx, decl, file, it->second, p.parent_path().string(), path);
  ctx.labels.pop_back();
  ctx.stack.erase(key);
}

void expand_in(Ctx& ctx, dsl::PatchAst& ast, const std::string& dir,
               const std::string& file) {
  for (size_t i = 0; i < ast.modules.size();) {
    if (ast.modules[i].type != "sub") {
      ++i;
      continue;
    }
    instantiate(ctx, ast, i, dir, file);
    if (ctx.failed()) return;
  }
}

}  // namespace

SubExpansion expand_subs(dsl::PatchAst& ast, const std::string& patch_dir,
                         const FileReader& read, Diags& errors) {
  SubExpansion out;
  bool any = false;
  for (const auto& m : ast.modules)
    if (m.type == "sub" || m.type == "subdef" || is_iface(m.type)) {
      any = true;
      break;
    }
  if (!any) return out;

  Ctx ctx{read, errors, errors.size(), {}, {}, {}, {}, {}};
  dsl::PatchAst work = ast;
  expand_in(ctx, work, patch_dir, "");
  if (!ctx.failed()) {
    ast = std::move(work);
    out.files = std::move(ctx.files);
  }
  return out;
}

}  // namespace ms::graph
