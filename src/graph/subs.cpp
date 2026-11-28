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
#include "dsl/suggest.h"

namespace ms::graph {

bool read_text_file(const std::string& path, std::string& out) {
  // Only regular files: a directory "opens" and reads as empty, and a device
  // or FIFO (/dev/zero) would never finish reading.
  std::error_code ec;
  if (!std::filesystem::is_regular_file(path, ec)) return false;
  std::ifstream f(path);
  if (!f) return false;
  std::ostringstream ss;
  ss << f.rdbuf();
  out = ss.str();
  return true;
}

namespace {

constexpr size_t kMaxDepth = 16;
// Nesting alone does not bound the work: a file that instantiates its child
// four times, nine deep, is already 4^9 modules, and an input fanned out at
// every level multiplies cables the same way. Cap the expanded size so a
// runaway patch reports line:col instead of hanging the session.
constexpr size_t kMaxExpanded = 1 << 16;

bool is_iface(const std::string& t) {
  return t == "param" || t == "input" || t == "output";
}

bool chain_under(const std::string& chain, const std::string& name) {
  return chain.size() > name.size() + 1 &&
         chain.compare(0, name.size(), name) == 0 && chain[name.size()] == '.';
}

// `name` itself or anything under it.
bool chain_in(const std::string& chain, const std::string& name) {
  return chain == name || chain_under(chain, name);
}

const dsl::ParamDecl* find_param(const dsl::ModuleDecl& m, const std::string& n) {
  for (const auto& p : m.params)
    if (p.name == n) return &p;
  return nullptr;
}

using ParamMap = std::unordered_map<std::string, dsl::Value>;
using TemplateMap = std::unordered_map<std::string, dsl::PatchAst>;

struct Ctx {
  const FileReader& read;
  Diags& errors;
  size_t err0 = 0;
  std::vector<std::string> files;
  std::unordered_set<std::string> file_set;
  std::unordered_map<std::string, dsl::PatchAst> cache;
  std::vector<std::string> labels;
  std::unordered_set<std::string> stack;
  SubOutputs outputs;

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

// --- Param substitution ------------------------------------------------------

void subst_value(const ParamMap& params, dsl::Value& v) {
  if (v.kind == dsl::Value::Kind::Ident) {
    auto it = params.find(v.str);
    if (it != params.end()) v = it->second;
  } else if (v.kind == dsl::Value::Kind::Array) {
    for (auto& item : v.items) subst_value(params, item);
  }
}

// The numeric value of param `name`, if `name` is a param; reports (and
// returns null) when it is one but not a number. `what` names the use.
const dsl::Value* numeric_param(Ctx& ctx, const std::string& file, const ParamMap& params,
                            const std::string& name, int line, int col, const char* what) {
  auto it = params.find(name);
  if (it == params.end()) return nullptr;
  if (it->second.kind != dsl::Value::Kind::Num) {
    ctx.err(file, line, col, "param '" + name + "' used as a " + what + " must be a number");
    return nullptr;
  }
  return &it->second;
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
      if (const dsl::Value* v = numeric_param(ctx, file, params, c.src.module, c.src.line,
                                              c.src.col, "cable source")) {
        c.src.is_literal = true;
        c.src.literal = v->num;
        c.src.unit = v->unit;  // checked against the input it drives
        c.src.module.clear();
      }
    }
    if (!c.depth_ident.empty()) {
      if (const dsl::Value* v = numeric_param(ctx, file, params, c.depth_ident, c.line, c.col,
                                              "cable depth")) {
        c.depth = v->num;
        c.depth_ident.clear();
      }
    }
  }
}

// --- Interface ---------------------------------------------------------------

struct Interface {
  ParamMap params;
  // Each input with its `default:` (a 0 at the input itself when none).
  std::vector<std::pair<std::string, dsl::ParamDecl>> inputs;
  std::vector<std::string> outputs;
  bool any() const { return !params.empty() || !inputs.empty() || !outputs.empty(); }
  bool is_input(const std::string& n) const {
    return std::any_of(inputs.begin(), inputs.end(), [&](const auto& i) { return i.first == n; });
  }
  bool is_output(const std::string& n) const {
    return std::find(outputs.begin(), outputs.end(), n) != outputs.end();
  }
};

// Declared top-level subdef names; their `name.*` members belong to the
// template, not to the enclosing file's interface.
std::vector<std::string> subdef_names(const dsl::PatchAst& body) {
  std::vector<std::string> out;
  for (const auto& m : body.modules)
    if (m.type == "subdef" && m.name.find('.') == std::string::npos) out.push_back(m.name);
  return out;
}

void scan_decl(Ctx& ctx, const std::string& file, const dsl::ModuleDecl& m, Interface& f) {
  const dsl::ParamDecl* d = find_param(m, "default");
  if (m.type == "param") {
    if (!d) return ctx.err(file, m.line, m.col, "param '" + m.name + "' requires 'default:'");
    f.params.emplace(m.name, d->value);
  } else if (m.type == "input") {
    const bool numeric = !d || d->value.kind == dsl::Value::Kind::Num;
    if (!numeric) ctx.err(file, d->line, d->col, "input '" + m.name + "' default must be a number");
    f.inputs.emplace_back(m.name, d && numeric ? *d : dsl::ParamDecl{"default", {}, m.line, m.col});
  } else if (m.type == "output") {
    f.outputs.push_back(m.name);
  }
}

Interface scan_interface(Ctx& ctx, const std::string& file, const dsl::PatchAst& body) {
  Interface f;
  const std::vector<std::string> subdefs = subdef_names(body);
  for (const auto& m : body.modules) {
    if (std::none_of(subdefs.begin(), subdefs.end(),
                     [&](const std::string& sd) { return chain_under(m.name, sd); }))
      scan_decl(ctx, file, m, f);
  }
  return f;
}

// --- Splicing an instance's ports into its parent ----------------------------
//
// Body cables touching the interface are lifted out: those reading an input
// become that input's targets, those driving an output become its sources.
// Each parent cable reading an instance output is then replaced by the
// output's sources, and each feeding an instance input fans out to the
// input's targets (depths multiply). Unfed inputs with a default drive their
// targets with it. `inst` is empty for a top-level patch splicing itself.
class Splice {
 public:
  Splice(Ctx& ctx, const std::string& body_file, const std::string& parent_file,
         const std::string& inst, const Interface& f)
      : ctx_(ctx), file_(body_file), parent_file_(parent_file), inst_(inst), f_(f) {}

  void run(dsl::PatchAst& parent, dsl::PatchAst& body) {
    if (!lift_ports(body)) return;
    for (const auto& m : body.modules) members_.insert(m.name.substr(0, m.name.find('.')));
    for (const auto& c : parent.cables)
      if (!route(c)) return;
    drive_defaults();
    parent.cables = std::move(out_);
    std::erase_if(body.modules, [](const dsl::ModuleDecl& m) { return is_iface(m.type); });
  }

 private:
  // The parent-side name of a body chain (port or internal module).
  std::string qualify(const std::string& chain) const {
    return inst_.empty() ? chain : inst_ + "." + chain;
  }

  // The output a body cable drives, through `o` or `o.in`; "" for none.
  std::string driven_output(const std::string& dst) const {
    if (f_.is_output(dst)) return dst;
    if (dst.size() > 3 && dst.ends_with(".in")) {
      std::string head = dst.substr(0, dst.size() - 3);
      if (f_.is_output(head)) return head;
    }
    return "";
  }

  bool lift_ports(dsl::PatchAst& body) {
    std::vector<dsl::CableDecl> kept;
    for (auto& c : body.cables) {
      const bool from_input = !c.src.is_literal && f_.is_input(c.src.module);
      const std::string out_name = driven_output(c.dst.module);
      if (from_input && !out_name.empty()) {
        ctx_.err(file_, c.line, c.col, "cable may not connect input '" + c.src.module +
                                           "' directly to output '" + out_name + "'");
        return false;
      }
      if (from_input) in_targets_[c.src.module].push_back(c);
      else if (!out_name.empty()) out_sources_[out_name].push_back(c);
      else kept.push_back(std::move(c));
    }
    body.cables = std::move(kept);
    for (const auto& c : body.cables)
      if (!c.src.is_literal && f_.is_output(c.src.module) && !inst_.empty())
        ctx_.err(file_, c.src.line, c.src.col,
                 "output '" + c.src.module + "' cannot be read inside its own subpatch");
    return !ctx_.failed();
  }

  // `inst.x`, where x is neither a port nor a body member, is a misspelled
  // port: name the interface instead of leaving compile to report an
  // unknown module 'inst.x'.
  // (A literal's empty name, and every name at top level where inst_ is
  // empty, are never chain_under it.)
  bool known_port(const dsl::Endpoint& e, const std::string& pfile) {
    if (!chain_under(e.module, inst_)) return true;
    const std::string x = e.module.substr(inst_.size() + 1);
    if (x.find('.') != std::string::npos || members_.count(x)) return true;
    std::vector<std::string> ins;
    for (const auto& i : f_.inputs) ins.push_back(i.first);
    std::vector<std::string> ports = ins;
    ports.insert(ports.end(), f_.outputs.begin(), f_.outputs.end());
    ctx_.err(pfile, e.line, e.col,
             "sub '" + inst_ + "' has no port '" + x + "'" + dsl::did_you_mean(x, ports) +
                 " (inputs: " + dsl::join(ins) + "; outputs: " + dsl::join(f_.outputs) + ")");
    return false;
  }

  bool route(const dsl::CableDecl& c) {
    // Errors here are about this parent-side cable: attribute them to the
    // file it was written in (empty = the parent itself), not the body.
    const std::string& pfile = c.file.empty() ? parent_file_ : c.file;
    if (!known_port(c.src, pfile) || !known_port(c.dst, pfile)) return false;
    std::vector<dsl::CableDecl> stage{c};
    if (!c.src.is_literal && !read_outputs(c, pfile, stage)) return false;
    for (const auto& sc : stage)
      if (!check_dst(sc, pfile) || !feed(sc, pfile)) return false;
    return true;
  }

  // A parent cable reading an instance output becomes one cable per source
  // driving that output inside the body.
  bool read_outputs(const dsl::CableDecl& c, const std::string& pfile,
                    std::vector<dsl::CableDecl>& stage) {
    std::string out_name;
    if (!inst_.empty() && c.src.module == inst_) {
      if (f_.outputs.empty()) {
        ctx_.err(pfile, c.src.line, c.src.col, "subpatch '" + inst_ + "' has no outputs");
        return false;
      }
      out_name = f_.outputs[0];
    } else {
      for (const auto& o : f_.outputs)
        if (c.src.module == qualify(o)) out_name = o;
    }
    for (const auto& i : f_.inputs)
      if (c.src.module == qualify(i.first)) {
        ctx_.err(pfile, c.src.line, c.src.col,
                 "input '" + i.first + "' of '" + inst_ + "' cannot be a source");
        return false;
      }
    if (out_name.empty()) return true;
    auto srcs = out_sources_.find(out_name);
    if (srcs == out_sources_.end() || srcs->second.empty()) {
      ctx_.err(pfile, c.src.line, c.src.col,
               "output '" + out_name + "'" + (inst_.empty() ? "" : " of '" + inst_ + "'") +
                   " is never driven inside the subpatch");
      return false;
    }
    stage.clear();
    for (const auto& s : srcs->second) {
      dsl::CableDecl nc = c;
      nc.src = s.src;
      if (!nc.src.is_literal) nc.src.module = qualify(s.src.module);
      nc.depth = c.depth * s.depth;
      stage.push_back(std::move(nc));
    }
    return true;
  }

  // Outputs are not destinations, and ports have no members.
  bool check_dst(const dsl::CableDecl& sc, const std::string& pfile) {
    const std::string& dst = sc.dst.module;
    for (const auto& o : f_.outputs) {
      if (dst == qualify(o)) {
        ctx_.err(pfile, sc.dst.line, sc.dst.col,
                 "output '" + o + "' of '" + inst_ + "' cannot be a destination");
        return false;
      }
      if (chain_under(dst, qualify(o))) {
        ctx_.err(pfile, sc.dst.line, sc.dst.col, "port '" + o + "' has no members");
        return false;
      }
    }
    for (const auto& i : f_.inputs)
      if (chain_under(dst, qualify(i.first))) {
        ctx_.err(pfile, sc.dst.line, sc.dst.col, "port '" + i.first + "' has no members");
        return false;
      }
    return true;
  }

  // A cable into an instance input fans out to the input's targets; any
  // other cable passes through.
  bool feed(const dsl::CableDecl& sc, const std::string& pfile) {
    std::string in_name;
    for (const auto& i : f_.inputs)
      if (sc.dst.module == qualify(i.first)) in_name = i.first;
    if (in_name.empty()) {
      out_.push_back(sc);
      return true;
    }
    fed_inputs_.insert(in_name);
    const auto& targets = in_targets_[in_name];
    if (out_.size() + targets.size() > kMaxExpanded) {
      ctx_.err(pfile, sc.line, sc.col,
               "subpatch expansion exceeds " + std::to_string(kMaxExpanded) + " cables");
      return false;
    }
    for (const auto& t : targets) {
      dsl::CableDecl nc = sc;
      nc.dst = t.dst;
      nc.dst.module = qualify(t.dst.module);
      nc.depth = sc.depth * t.depth;
      out_.push_back(std::move(nc));
    }
    return true;
  }

  void drive_defaults() {
    for (const auto& [name, def] : f_.inputs) {
      if (fed_inputs_.count(name) || def.value.num == 0.0) continue;
      for (const auto& t : in_targets_[name]) {
        dsl::CableDecl nc = t;
        nc.src = {};
        nc.src.is_literal = true;
        nc.src.literal = def.value.num;
        nc.src.unit = def.value.unit;  // a mismatch reports at the default
        nc.src.line = def.line;
        nc.src.col = def.col;
        nc.dst.module = qualify(t.dst.module);
        out_.push_back(std::move(nc));
      }
    }
  }

  Ctx& ctx_;
  const std::string file_;
  const std::string parent_file_;
  const std::string inst_;
  const Interface& f_;
  std::unordered_map<std::string, std::vector<dsl::CableDecl>> in_targets_, out_sources_;
  std::unordered_set<std::string> fed_inputs_;
  std::unordered_set<std::string> members_;  // body names' first segments, ports included
  std::vector<dsl::CableDecl> out_;
};

// --- demo.* sections -----------------------------------------------------------

bool in_demo(const std::string& chain) { return chain_in(chain, "demo"); }

bool refs_out(const dsl::Endpoint& e) { return !e.is_literal && chain_in(e.module, "out"); }

// A top-level subpatch file plays its demo section: demo.out becomes out.
void demo_out_to_out(dsl::Endpoint& e) {
  if (!e.is_literal && chain_in(e.module, "demo.out")) e.module.erase(0, std::string("demo.").size());
}

// A body may not reach the real `out`; at top level demo.* plays, inside an
// instance it is dropped.
void handle_demo(Ctx& ctx, const std::string& file, dsl::PatchAst& body, bool top) {
  for (const auto& m : body.modules)
    if (is_iface(m.type) && in_demo(m.name))
      ctx.err(file, m.line, m.col,
              "'" + m.name + "': interface declarations may not live under demo.*");
  for (const auto& c : body.cables)
    if (refs_out(c.src) || refs_out(c.dst))
      ctx.err(file, c.line, c.col,
              "subpatch bodies may not reference 'out' (use demo.out in a demo section)");
  if (ctx.failed()) return;

  if (top) {
    for (auto& c : body.cables) {
      demo_out_to_out(c.src);
      demo_out_to_out(c.dst);
    }
    return;
  }
  std::erase_if(body.modules, [](const dsl::ModuleDecl& m) { return in_demo(m.name); });
  std::erase_if(body.cables, [](const dsl::CableDecl& c) {
    return (!c.src.is_literal && in_demo(c.src.module)) || in_demo(c.dst.module);
  });
}

// --- Instantiation ---------------------------------------------------------------

void expand_in(Ctx& ctx, dsl::PatchAst& ast, const std::string& dir,
               const std::string& file);

// The template's params with the instance's overrides applied.
ParamMap bind_params(Ctx& ctx, const dsl::ModuleDecl& decl, const std::string& parent_file,
                     ParamMap params) {
  for (const auto& o : decl.params) {
    if (o.name == "file" || o.name == "def") continue;
    auto it = params.find(o.name);
    if (it == params.end()) {
      std::vector<std::string> names;
      for (const auto& [n, v] : params) names.push_back(n);
      std::sort(names.begin(), names.end());
      ctx.err(parent_file, o.line, o.col, "sub '" + decl.name + "' has no param '" + o.name + "'" +
                                              dsl::did_you_mean(o.name, names) +
                                              " (params: " + dsl::join(names) + ")");
    }
    else
      it->second = o.value;
  }
  return params;
}

// Prefix every body name (and every config value naming one) with `inst.`.
void stamp_body(dsl::PatchAst& body, const std::string& inst, const std::string& body_file) {
  std::unordered_set<std::string> internal;
  for (const auto& m : body.modules) internal.insert(m.name);
  for (auto& m : body.modules) {
    for (auto& p : m.params)
      if (p.value.kind == dsl::Value::Kind::Ident && internal.count(p.value.str))
        p.value.str = inst + "." + p.value.str;
    m.name = inst + "." + m.name;
    if (m.file.empty()) m.file = body_file;
  }
  for (auto& c : body.cables) {
    if (!c.src.is_literal) c.src.module = inst + "." + c.src.module;
    c.dst.module = inst + "." + c.dst.module;
    if (c.file.empty()) c.file = body_file;
  }
}

void do_body(Ctx& ctx, dsl::PatchAst& parent, size_t idx, const dsl::ModuleDecl& decl,
             const std::string& parent_file, dsl::PatchAst body,
             const std::string& body_dir, const std::string& body_file) {
  const Interface f = scan_interface(ctx, body_file, body);
  if (ctx.failed()) return;
  const ParamMap params = bind_params(ctx, decl, parent_file, f.params);
  if (ctx.failed()) return;
  substitute(ctx, body_file, body, params);
  if (ctx.failed()) return;
  handle_demo(ctx, body_file, body, /*top=*/false);
  if (ctx.failed()) return;
  const size_t nested = ctx.outputs.size();
  expand_in(ctx, body, body_dir, body_file);
  if (ctx.failed()) return;
  if (parent.modules.size() + body.modules.size() > kMaxExpanded)
    return ctx.err(parent_file, decl.line, decl.col,
                   "subpatch expansion exceeds " + std::to_string(kMaxExpanded) + " modules");
  Splice(ctx, body_file, parent_file, decl.name, f).run(parent, body);
  if (ctx.failed()) return;

  stamp_body(body, decl.name, body_file);
  for (size_t i = nested; i < ctx.outputs.size(); ++i)
    ctx.outputs[i].first = decl.name + "." + ctx.outputs[i].first;
  ctx.outputs.emplace_back(decl.name, f.outputs);
  parent.modules.erase(parent.modules.begin() + static_cast<long>(idx));
  parent.modules.insert(parent.modules.begin() + static_cast<long>(idx),
                        std::make_move_iterator(body.modules.begin()),
                        std::make_move_iterator(body.modules.end()));
  parent.cables.insert(parent.cables.end(),
                       std::make_move_iterator(body.cables.begin()),
                       std::make_move_iterator(body.cables.end()));
}

// Where an instance's body comes from: a template AST plus the directory and
// file its own includes and diagnostics resolve against. `key` marks it on
// the include stack for cycle detection.
struct Source {
  const dsl::PatchAst* ast = nullptr;
  std::string dir, file, key;
};

Source def_source(Ctx& ctx, const dsl::ParamDecl& pd,
                  const TemplateMap& templates, const std::string& dir, const std::string& file) {
  if (pd.value.kind != dsl::Value::Kind::Ident) {
    ctx.err(file, pd.line, pd.col, "sub 'def:' expects a template name");
    return {};
  }
  auto it = templates.find(pd.value.str);
  if (it == templates.end()) {
    ctx.err(file, pd.line, pd.col, "unknown subdef '" + pd.value.str + "'");
    return {};
  }
  return {&it->second, dir, file, "def:" + pd.value.str + "@" + file};
}

// Parse a subpatch file once per expansion; later instances reuse the AST.
const dsl::PatchAst* load_file(Ctx& ctx, const std::string& path, const dsl::ParamDecl& pf,
                               const std::string& file) {
  auto it = ctx.cache.find(path);
  if (it != ctx.cache.end()) return &it->second;
  std::string src;
  if (!ctx.read(path, src)) {
    ctx.err(file, pf.line, pf.col, "cannot read subpatch '" + path + "'");
    return nullptr;
  }
  auto pr = dsl::parse(src);
  if (!pr.ok()) {
    for (const auto& d : pr.errors) ctx.err(path, d.line, d.col, d.msg);
    return nullptr;
  }
  if (ctx.file_set.insert(path).second) ctx.files.push_back(path);
  return &ctx.cache.emplace(path, std::move(pr.ast)).first->second;
}

Source file_source(Ctx& ctx, const dsl::ModuleDecl& decl, const dsl::ParamDecl& pf,
                   const std::string& dir, const std::string& file) {
  if (pf.value.kind != dsl::Value::Kind::Str) {
    ctx.err(file, pf.line, pf.col, "sub 'file:' expects a quoted path");
    return {};
  }
  const std::filesystem::path p = (std::filesystem::path(dir) / pf.value.str).lexically_normal();
  const std::string path = p.string();
  const std::string key = "file:" + path;
  if (ctx.stack.count(key)) {
    ctx.err(file, decl.line, decl.col, "subpatch include cycle at '" + path + "'");
    return {};
  }
  return {load_file(ctx, path, pf, file), p.parent_path().string(), path, key};
}

void instantiate(Ctx& ctx, dsl::PatchAst& parent, size_t idx, const TemplateMap& templates,
                 const std::string& dir, const std::string& file) {
  const dsl::ModuleDecl decl = parent.modules[idx];
  const dsl::ParamDecl* pf = find_param(decl, "file");
  const dsl::ParamDecl* pd = find_param(decl, "def");
  if ((pf != nullptr) == (pd != nullptr))
    return ctx.err(file, decl.line, decl.col,
                   "sub '" + decl.name + "' requires exactly one of 'file:' or 'def:'");
  if (ctx.stack.size() >= kMaxDepth)
    return ctx.err(file, decl.line, decl.col, "subpatch nesting deeper than 16");
  const Source src = pd ? def_source(ctx, *pd, templates, dir, file)
                        : file_source(ctx, decl, *pf, dir, file);
  if (!src.ast) return;
  ctx.stack.insert(src.key);
  ctx.labels.push_back(decl.name + " at " + (file.empty() ? "patch" : file) + ":" +
                       std::to_string(decl.line));
  do_body(ctx, parent, idx, decl, file, *src.ast, src.dir, src.file);
  ctx.labels.pop_back();
  ctx.stack.erase(src.key);
}

// --- Inline subdef templates -----------------------------------------------------

struct SubdefDecl { std::string name; int line, col; };

std::vector<SubdefDecl> find_subdefs(Ctx& ctx, const std::string& file, const dsl::PatchAst& ast) {
  std::vector<SubdefDecl> out;
  for (const auto& m : ast.modules) {
    if (m.type != "subdef") continue;
    if (m.name.find('.') != std::string::npos)
      ctx.err(file, m.line, m.col, "subdef '" + m.name + "' may not be nested (dotted name)");
    else
      out.push_back({m.name, m.line, m.col});
  }
  return out;
}

std::string subdef_owner(const std::vector<SubdefDecl>& defs, const std::string& chain) {
  for (const auto& d : defs)
    if (chain_under(chain, d.name)) return d.name;
  return "";
}

// `g.x` -> `x` when the name lives under template `g`.
void unprefix(std::string& chain, const std::string& g) {
  if (chain_under(chain, g)) chain.erase(0, g.size() + 1);
}

// Move each template member (`g.*` modules, then the cables among them) out
// of `ast` into its template, with the `g.` prefix removed.
void take_modules(dsl::PatchAst& ast, const std::vector<SubdefDecl>& defs, TemplateMap& out) {
  std::vector<dsl::ModuleDecl> keep_m;
  for (auto& m : ast.modules) {
    if (m.type == "subdef") continue;
    const std::string g = subdef_owner(defs, m.name);
    if (g.empty()) {
      keep_m.push_back(std::move(m));
      continue;
    }
    unprefix(m.name, g);
    for (auto& p : m.params) {
      if (p.value.kind == dsl::Value::Kind::Ident) unprefix(p.value.str, g);
      if (p.value.kind == dsl::Value::Kind::Array)
        for (auto& item : p.value.items)
          if (item.kind == dsl::Value::Kind::Ident) unprefix(item.str, g);
    }
    out[g].modules.push_back(std::move(m));
  }
  ast.modules = std::move(keep_m);
}

void take_cables(Ctx& ctx, const std::string& file, dsl::PatchAst& ast,
                 const std::vector<SubdefDecl>& defs, TemplateMap& out) {
  std::vector<dsl::CableDecl> keep_c;
  for (auto& c : ast.cables) {
    const std::string sg = c.src.is_literal ? "" : subdef_owner(defs, c.src.module);
    const std::string dg = subdef_owner(defs, c.dst.module);
    if (sg.empty() && dg.empty()) {
      keep_c.push_back(std::move(c));
      continue;
    }
    // A literal may feed a member; anything else must stay inside one template.
    if (!c.src.is_literal && sg != dg) {
      ctx.err(file, c.line, c.col, "cable crosses subdef '" + (sg.empty() ? dg : sg) + "' boundary");
      continue;
    }
    if (!c.src.is_literal) unprefix(c.src.module, dg);
    unprefix(c.dst.module, dg);
    unprefix(c.depth_ident, dg);
    out[dg].cables.push_back(std::move(c));
  }
  ast.cables = std::move(keep_c);
}

TemplateMap collect_subdefs(Ctx& ctx, const std::string& file, dsl::PatchAst& ast) {
  TemplateMap out;
  const std::vector<SubdefDecl> defs = find_subdefs(ctx, file, ast);
  for (const auto& d : defs) out.emplace(d.name, dsl::PatchAst{});
  if (out.empty() || ctx.failed()) return out;
  take_modules(ast, defs, out);
  take_cables(ctx, file, ast, defs, out);
  for (const auto& d : defs)
    if (out.at(d.name).modules.empty())
      ctx.err(file, d.line, d.col, "subdef '" + d.name + "' has no members");
  return out;
}

void expand_in(Ctx& ctx, dsl::PatchAst& ast, const std::string& dir,
               const std::string& file) {
  const TemplateMap templates = collect_subdefs(ctx, file, ast);
  if (ctx.failed()) return;
  for (size_t i = 0; i < ast.modules.size();) {
    if (ast.modules[i].type != "sub") {
      ++i;
      continue;
    }
    instantiate(ctx, ast, i, templates, dir, file);
    if (ctx.failed()) return;
  }
}

}  // namespace

SubExpansion expand_subs(dsl::PatchAst& ast, const std::string& patch_dir,
                         const FileReader& read, Diags& errors) {
  SubExpansion out;
  if (std::none_of(ast.modules.begin(), ast.modules.end(), [](const dsl::ModuleDecl& m) {
        return m.type == "sub" || m.type == "subdef" || is_iface(m.type);
      }))
    return out;

  Ctx ctx{read, errors, errors.size(), {}, {}, {}, {}, {}, {}};
  dsl::PatchAst work = ast;
  // A subpatch file checked on its own is its own instance: its params take
  // their defaults, its demo section plays, and its ports splice into itself.
  const Interface top = scan_interface(ctx, "", work);
  if (top.any() && !ctx.failed()) substitute(ctx, "", work, top.params);
  if (!ctx.failed()) expand_in(ctx, work, patch_dir, "");
  if (top.any() && !ctx.failed()) {
    handle_demo(ctx, "", work, /*top=*/true);
    if (!ctx.failed()) Splice(ctx, "", "", "", top).run(work, work);
  }
  if (!ctx.failed()) {
    ast = std::move(work);
    out.files = std::move(ctx.files);
    out.outputs = std::move(ctx.outputs);
  }
  return out;
}

PatchBuild build_patch(std::string_view src, const std::string& patch_path, double rate) {
  PatchBuild b;
  auto p = dsl::parse(src);
  b.compiled.errors = std::move(p.errors);
  if (!b.compiled.ok()) return b;
  const std::filesystem::path parent = std::filesystem::path(patch_path).parent_path();
  const std::string dir = parent.empty() ? "." : parent.string();
  SubExpansion x = expand_subs(p.ast, dir, read_text_file, b.compiled.errors);
  b.files = std::move(x.files);
  b.sub_outputs = std::move(x.outputs);
  if (b.compiled.ok()) embed_tracker_files(p.ast, dir, read_text_file, b.files, b.compiled.errors);
  if (!b.compiled.ok()) return b;
  b.ast = std::move(p.ast);
  b.compiled = compile(b.ast, rate);
  return b;
}

}  // namespace ms::graph
