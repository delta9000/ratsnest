#include "graph/graph.h"
#include <algorithm>
#include <cmath>
#include <functional>
#include <limits>
#include <queue>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include "dsl/suggest.h"
#include "graph/voices.h"

namespace ms::graph {
namespace {

template <class Items>
std::vector<std::string> names_of(const Items& items) {
  std::vector<std::string> v;
  for (const auto& it : items) v.emplace_back(it.name);
  return v;
}

// Why `port` is not one of `wanted`: it names the other direction, or it is
// close to a real name.
std::string port_hint(const std::string& port, const std::vector<std::string>& wanted,
                      const std::vector<std::string>& other, const char* other_kind) {
  if (std::find(other.begin(), other.end(), port) != other.end())
    return " ('" + port + "' is " + other_kind + "; cables run source -> destination)";
  return dsl::did_you_mean(port, wanted);
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
// A number never matches: its str is empty, and no enum value is.
std::string enum_error(const dsp::ConfigInfo& ci, const dsl::Value& v) {
  std::vector<std::string> vals;
  for (const char* const* e = ci.enum_vals; *e; ++e) vals.emplace_back(*e);
  auto valid = [&](const dsl::Value& item) {
    return std::find(vals.begin(), vals.end(), item.str) != vals.end();
  };
  const bool array = ci.kind == dsp::ConfigKind::Enums;
  if (array ? (v.kind == dsl::Value::Kind::Array && std::all_of(v.items.begin(), v.items.end(), valid))
            : valid(v)) return {};
  return std::string(array ? "expects an array of: " : "expects one of: ") + dsl::join(vals) +
         dsl::did_you_mean(v.str, vals);
}

bool finite_num(const dsl::Value& v) {
  return v.kind == dsl::Value::Kind::Num && std::isfinite(v.num);
}

std::string config_error(const dsp::ConfigInfo& ci, const dsl::Value& v) {
  using K = dsl::Value::Kind;
  using C = dsp::ConfigKind;
  if (ci.enum_vals) return enum_error(ci, v);
  auto text = [](const dsl::Value& x) { return x.kind == K::Str || x.kind == K::Ident; };
  if (ci.kind == C::Text) return text(v) ? "" : "expects text";
  if (ci.kind == C::Texts)
    return v.kind == K::Array && std::all_of(v.items.begin(), v.items.end(), text)
        ? "" : "expects an array of names";
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
    if (!info) {
      failed_.insert(m.name);  // its cables would only repeat this error
      return err(m.line, m.col, "unknown module type '" + m.type + "'" +
                 dsl::did_you_mean(m.type, module_types(), " (run 'ratsnest describe' for the list)"),
                 m.file);
    }
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
        return err(p.line, p.col, "parameter '" + p.name + "' expects a finite float number" +
                   // `a4` lexes as a name: notes are the uppercase spelling.
                   (p.value.kind == dsl::Value::Kind::Ident ? " (note names are uppercase, e.g. A4)" : ""),
                   m.file);
      n.input_base[static_cast<size_t>(slot)] = static_cast<float>(p.value.num);
      return;
    }
    if (const dsp::ConfigInfo* ci = info.config(p.name)) {
      const std::string problem = config_error(*ci, p.value);
      if (!problem.empty()) return err(p.line, p.col, "parameter '" + p.name + "' " + problem, m.file);
      n.config.push_back({p.name, to_config_value(p.value)});
      return;
    }
    std::vector<std::string> all = names_of(info.inputs);
    for (const auto& c : names_of(info.configs)) all.push_back(c);
    err(p.line, p.col, "module type '" + m.type + "' has no parameter '" + p.name + "'" +
        dsl::did_you_mean(p.name, all) + " (parameters: " + dsl::join(all) + ")", m.file);
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
    if (rd.node < 0) return unknown_module(c.dst, c.file);
    Node& dn = g_->nodes[static_cast<size_t>(rd.node)];
    const std::string dport = rd.port.empty() ? "in" : rd.port;
    const int dslot = dn.info->input_index(dport);
    if (dslot < 0) {
      const auto inputs = names_of(dn.info->inputs);
      return err(c.dst.line, c.dst.col, "module '" + dn.name + "' has no input '" + dport + "'" +
                 port_hint(dport, inputs, names_of(dn.info->outputs), "an output") +
                 " (inputs: " + dsl::join(inputs) + ")", c.file);
    }
    if (c.src.is_literal) {
      dn.input_base[static_cast<size_t>(dslot)] += static_cast<float>(c.src.literal * c.depth);
      return;
    }
    const Resolved rs = resolve(c.src.module);
    if (rs.node < 0) return unknown_module(c.src, c.file);
    const Node& sn = g_->nodes[static_cast<size_t>(rs.node)];
    // A bare module name means its first output (spec §DSL; e.g. `seq -> ...`
    // reads stepseq's cv, not a port literally named "out").
    const std::string sport = rs.port.empty()
        ? (sn.info->outputs.empty() ? "" : sn.info->outputs[0].name)
        : rs.port;
    const int sslot = sn.info->output_index(sport);
    if (sslot < 0) {
      const auto outputs = names_of(sn.info->outputs);
      return err(c.src.line, c.src.col, "module '" + sn.name + "' has no output '" + sport + "'" +
                 port_hint(sport, outputs, names_of(sn.info->inputs), "an input") +
                 " (outputs: " + dsl::join(outputs) + ")", c.file);
    }
    cables.push_back({rs.node, sslot, rd.node, dslot, static_cast<float>(c.depth)});
  }

  // An endpoint naming no node: quiet when its declaration already failed,
  // else a suggestion from the declared (not voice-stamped) names.
  void unknown_module(const dsl::Endpoint& e, const std::string& file) {
    if (split_endpoint([&](const std::string& n) { return failed_.count(n) > 0; }, e.module).ok)
      return;
    std::vector<std::string> names;
    for (const auto& [name, idx] : index_of_)
      if (name.find('#') == std::string::npos) names.push_back(name);
    std::sort(names.begin(), names.end());
    const std::string head = e.module.substr(0, e.module.find('.'));
    err(e.line, e.col, "unknown module '" + e.module + "'" + dsl::did_you_mean(head, names), file);
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
  std::unordered_set<std::string> failed_;  // declared with an unknown type
};

}  // namespace

std::vector<std::string> module_types() {
  std::vector<std::string> v;
  for (const auto* m : dsp::all_modules()) v.emplace_back(m->type);
  return v;
}

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
