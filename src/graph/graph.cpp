#include "graph/graph.h"
#include <algorithm>
#include <cmath>
#include <functional>
#include <limits>
#include <queue>
#include <string>
#include <unordered_map>
#include "graph/voices.h"

namespace ms::graph {
namespace {

std::string join_names(auto&& items, auto&& name_of) {
  std::string s;
  for (auto&& it : items) {
    if (!s.empty()) s += ", ";
    s += name_of(it);
  }
  return s;
}

dsp::ConfigValue to_config_value(const dsl::Value& v) {
  dsp::ConfigValue cv;
  switch (v.kind) {
    case dsl::Value::Kind::Num:   cv.is_num = true; cv.num = v.num; break;
    case dsl::Value::Kind::Ident:
    case dsl::Value::Kind::Str:   cv.str = v.str; break;
    case dsl::Value::Kind::Array:
      for (const auto& item : v.items) {
        if (item.kind == dsl::Value::Kind::Num) cv.arr.push_back(item.num);
        else cv.strs.push_back(item.str);  // Ident or Str
      }
      break;
  }
  return cv;
}

// An enum config (one name, or an array of names for ConfigKind::Enums).
std::string enum_error(const dsp::ConfigInfo& ci, const dsl::Value& v) {
  using K = dsl::Value::Kind;
  auto valid = [&](const dsl::Value& item) {
    if (item.kind != K::Ident && item.kind != K::Str) return false;
    for (const char* const* e = ci.enum_vals; *e; ++e)
      if (item.str == *e) return true;
    return false;
  };
  const bool array = ci.kind == dsp::ConfigKind::Enums;
  if (array ? (v.kind == K::Array && std::all_of(v.items.begin(), v.items.end(), valid))
            : valid(v)) return {};
  std::string opts;
  for (const char* const* e = ci.enum_vals; *e; ++e) {
    if (!opts.empty()) opts += ", ";
    opts += *e;
  }
  return std::string(array ? "expects an array of: " : "expects one of: ") + opts;
}

bool finite_num(const dsl::Value& v) {
  return v.kind == dsl::Value::Kind::Num && std::isfinite(v.num);
}

std::string config_error(const dsp::ConfigInfo& ci, const dsl::Value& v) {
  using K = dsl::Value::Kind;
  using C = dsp::ConfigKind;
  if (ci.enum_vals) return enum_error(ci, v);
  if (ci.kind == C::Text)
    return v.kind == K::Str || v.kind == K::Ident ? "" : "expects text";
  if (ci.kind == C::Numbers) {
    if (v.kind == K::Array && std::all_of(v.items.begin(), v.items.end(), finite_num)) return {};
    return "expects an array of finite numbers (note names use uppercase, e.g. D2v)";
  }
  return finite_num(v) ? "" : "expects a finite number";
}

// Kahn's algorithm over declaration order: always place the lowest-index
// node with no unplaced producers; a cycle is broken at the lowest-index
// node still unplaced. Self-loops never block. Returns the placement order.
std::vector<int> topo_order(size_t n, const std::vector<Cable>& cables) {
  std::vector<int> indegree(n, 0);
  std::vector<std::vector<int>> consumers(n);
  for (const auto& c : cables) {
    if (c.src_mod == c.dst_mod) continue;
    ++indegree[static_cast<size_t>(c.dst_mod)];
    consumers[static_cast<size_t>(c.src_mod)].push_back(c.dst_mod);
  }
  std::priority_queue<int, std::vector<int>, std::greater<>> ready;
  for (size_t i = 0; i < n; ++i)
    if (indegree[i] == 0) ready.push(static_cast<int>(i));
  std::vector<int> order;
  std::vector<bool> placed(n, false);
  size_t lowest_unplaced = 0;
  while (order.size() < n) {
    while (placed[lowest_unplaced]) ++lowest_unplaced;
    int pick = static_cast<int>(lowest_unplaced);  // cycle: force it
    if (!ready.empty()) { pick = ready.top(); ready.pop(); }
    placed[static_cast<size_t>(pick)] = true;
    order.push_back(pick);
    for (int dst : consumers[static_cast<size_t>(pick)])
      if (!placed[static_cast<size_t>(dst)] && --indegree[static_cast<size_t>(dst)] == 0)
        ready.push(dst);
  }
  return order;
}

class Compiler {
 public:
  Compiler(CompileResult& r, double sample_rate) : r_(r), g_(std::make_unique<Graph>()) {
    g_->sample_rate = sample_rate;
  }

  void run(const dsl::PatchAst& ast) {
    // --- Nodes: declared modules + implicit `out` ------------------------
    for (const auto& m : ast.modules) declare(m);
    add_node("out", dsp::find_module("out"));
    // --- Cables -----------------------------------------------------------
    std::vector<Cable> cables;
    for (const auto& c : ast.cables) connect(c, cables);
    if (!r_.errors.empty()) return;
    sort(cables);
    // --- Instantiate ------------------------------------------------------
    for (auto& node : g_->nodes) {
      node.instance = node.info->create();
      node.instance->init(g_->sample_rate, node.config);
    }
    r_.graph = std::move(g_);
  }

 private:
  void err(int line, int col, std::string msg, const std::string& file) {
    r_.errors.push_back({line, col, std::move(msg), file});
  }

  Node& add_node(const std::string& name, const dsp::ModuleInfo* info) {
    Node n;
    n.name = name;
    n.info = info;
    n.input_base.reserve(info->inputs.size());
    for (const auto& in : info->inputs) n.input_base.push_back(in.def);
    index_of_[name] = static_cast<int>(g_->nodes.size());
    g_->nodes.push_back(std::move(n));
    return g_->nodes.back();
  }

  void declare(const dsl::ModuleDecl& m) {
    if (m.name == "out") return err(m.line, m.col, "'out' is a reserved built-in module name", m.file);
    const dsp::ModuleInfo* info = dsp::find_module(m.type);
    if (!info) return err(m.line, m.col, "unknown module type '" + m.type + "'", m.file);
    // The parser rejects duplicates as written; this catches a declared name
    // colliding with one a subpatch stamps (`t.osc` beside `t = sub {..}`).
    if (index_of_.count(m.name))
      return err(m.line, m.col, "duplicate module name '" + m.name + "' (also stamped by a subpatch)", m.file);
    Node& n = add_node(m.name, info);
    for (const auto& p : m.params) apply_param(n, m, p);
    if (info->validate) {
      std::string msg = info->validate(n.config);
      if (!msg.empty()) err(m.line, m.col, std::move(msg), m.file);
    }
  }

  // A declared param is either an input's base value or a config entry.
  void apply_param(Node& n, const dsl::ModuleDecl& m, const dsl::ParamDecl& p) {
    const dsp::ModuleInfo& info = *n.info;
    const int slot = info.input_index(p.name);
    if (slot >= 0) {
      if (!finite_num(p.value) || std::abs(p.value.num) > std::numeric_limits<float>::max())
        return err(p.line, p.col, "parameter '" + p.name + "' expects a finite float number", m.file);
      n.input_base[static_cast<size_t>(slot)] = static_cast<float>(p.value.num);
      return;
    }
    if (const dsp::ConfigInfo* ci = info.config(p.name)) {
      const std::string problem = config_error(*ci, p.value);
      if (!problem.empty()) return err(p.line, p.col, "parameter '" + p.name + "' " + problem, m.file);
      n.config.push_back({p.name, to_config_value(p.value)});
      return;
    }
    err(p.line, p.col, "module type '" + m.type + "' has no parameter '" + p.name +
        "' (inputs: " + join_names(info.inputs, [](auto& i) { return i.name; }) + ")", m.file);
  }

  // Resolve a dotted chain to a node index + port via the shared split rule
  // (see graph/voices.h split_endpoint): longest declared name wins.
  struct Resolved { int node = -1; std::string port; };
  Resolved resolve(const std::string& chain) const {
    SplitEndpoint s =
        split_endpoint([&](const std::string& n) { return index_of_.count(n) > 0; }, chain);
    if (!s.ok) return {};
    return {index_of_.at(s.module), std::move(s.port)};
  }

  void connect(const dsl::CableDecl& c, std::vector<Cable>& cables) {
    if (!c.depth_ident.empty())
      return err(c.line, c.col, "unknown name '" + c.depth_ident +
                 "' for cable depth (params resolve only inside a subpatch)", c.file);
    const Resolved rd = resolve(c.dst.module);
    if (rd.node < 0) return err(c.dst.line, c.dst.col, "unknown module '" + c.dst.module + "'", c.file);
    Node& dn = g_->nodes[static_cast<size_t>(rd.node)];
    const std::string dport = rd.port.empty() ? "in" : rd.port;
    const int dslot = dn.info->input_index(dport);
    if (dslot < 0)
      return err(c.dst.line, c.dst.col, "module '" + dn.name + "' has no input '" + dport +
                 "' (inputs: " + join_names(dn.info->inputs, [](auto& i) { return i.name; }) + ")", c.file);
    if (c.src.is_literal) {
      dn.input_base[static_cast<size_t>(dslot)] += static_cast<float>(c.src.literal * c.depth);
      return;
    }
    const Resolved rs = resolve(c.src.module);
    if (rs.node < 0) return err(c.src.line, c.src.col, "unknown module '" + c.src.module + "'", c.file);
    const Node& sn = g_->nodes[static_cast<size_t>(rs.node)];
    // A bare module name means its first output (spec §DSL; e.g. `seq -> ...`
    // reads stepseq's cv, not a port literally named "out").
    const std::string sport = rs.port.empty()
        ? (sn.info->outputs.empty() ? "" : sn.info->outputs[0].name)
        : rs.port;
    const int sslot = sn.info->output_index(sport);
    if (sslot < 0)
      return err(c.src.line, c.src.col, "module '" + sn.name + "' has no output '" + sport +
                 "' (outputs: " + join_names(sn.info->outputs, [](auto& o) { return o.name; }) + ")", c.file);
    cables.push_back({rs.node, sslot, rd.node, dslot, static_cast<float>(c.depth)});
  }

  // Reorder nodes into processing order and bucket cables by destination.
  void sort(const std::vector<Cable>& cables) {
    const size_t n = g_->nodes.size();
    const std::vector<int> order = topo_order(n, cables);
    std::vector<int> new_index(n);
    for (size_t pos = 0; pos < n; ++pos) new_index[static_cast<size_t>(order[pos])] = static_cast<int>(pos);
    std::vector<Node> sorted;
    sorted.reserve(n);
    for (int old : order) sorted.push_back(std::move(g_->nodes[static_cast<size_t>(old)]));
    g_->nodes = std::move(sorted);
    g_->in_cables.assign(n, {});
    for (auto c : cables) {
      c.src_mod = new_index[static_cast<size_t>(c.src_mod)];
      c.dst_mod = new_index[static_cast<size_t>(c.dst_mod)];
      g_->in_cables[static_cast<size_t>(c.dst_mod)].push_back(c);
    }
    for (size_t i = 0; i < n; ++i)
      if (g_->nodes[i].name == "out") g_->out_node = static_cast<int>(i);
  }

  CompileResult& r_;
  std::unique_ptr<Graph> g_;
  std::unordered_map<std::string, int> index_of_;
};

}  // namespace

CompileResult compile(const dsl::PatchAst& ast_in, double sample_rate) {
  CompileResult r;
  // Voice groups expand first (spec §3); everything downstream is ordinary.
  dsl::PatchAst ast = ast_in;
  expand_voices(ast, r.errors);
  if (!r.errors.empty()) return r;
  Compiler(r, sample_rate).run(ast);
  return r;
}

}  // namespace ms::graph
