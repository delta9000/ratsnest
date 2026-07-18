#include "graph/graph.h"
#include <algorithm>
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

// Resolve a dotted chain to a node index + port via the shared split rule
// (see graph/voices.h split_endpoint): longest declared name wins.
struct Resolved { int node = -1; std::string port; };
Resolved resolve(const std::unordered_map<std::string, int>& index_of,
                  const std::string& chain) {
  SplitEndpoint s =
      split_endpoint([&](const std::string& n) { return index_of.count(n) > 0; }, chain);
  if (!s.ok) return {};
  return {index_of.at(s.module), std::move(s.port)};
}

}  // namespace

CompileResult compile(const dsl::PatchAst& ast_in, double sample_rate) {
  CompileResult r;
  // Voice groups expand first (spec §3); everything downstream is ordinary.
  dsl::PatchAst ast = ast_in;
  expand_voices(ast, r.errors);
  if (!r.errors.empty()) return r;

  auto g = std::make_unique<Graph>();
  g->sample_rate = sample_rate;
  auto err = [&](int line, int col, std::string msg) {
    r.errors.push_back({line, col, std::move(msg)});
  };

  // --- Nodes: declared modules + implicit `out` -------------------------
  std::unordered_map<std::string, int> index_of;
  auto add_node = [&](const std::string& name, const dsp::ModuleInfo* info) {
    Node n;
    n.name = name;
    n.info = info;
    n.input_base.reserve(info->inputs.size());
    for (const auto& in : info->inputs) n.input_base.push_back(in.def);
    index_of[name] = static_cast<int>(g->nodes.size());
    g->nodes.push_back(std::move(n));
  };

  for (const auto& m : ast.modules) {
    if (m.name == "out") { err(m.line, m.col, "'out' is a reserved built-in module name"); continue; }
    const dsp::ModuleInfo* info = dsp::find_module(m.type);
    if (!info) { err(m.line, m.col, "unknown module type '" + m.type + "'"); continue; }
    add_node(m.name, info);
    Node& n = g->nodes.back();
    for (const auto& p : m.params) {
      int slot = info->input_index(p.name);
      if (slot >= 0) {
        if (p.value.kind != dsl::Value::Kind::Num) {
          err(p.line, p.col, "parameter '" + p.name + "' expects a number");
        } else {
          n.input_base[static_cast<size_t>(slot)] = static_cast<float>(p.value.num);
        }
        continue;
      }
      if (const dsp::ConfigInfo* ci = info->config(p.name)) {
        dsp::ConfigValue cv = to_config_value(p.value);
        if (ci->enum_vals) {
          bool valid = false;
          for (const char* const* e = ci->enum_vals; *e; ++e)
            if (cv.str == *e) valid = true;
          if (!valid) {
            std::string opts;
            for (const char* const* e = ci->enum_vals; *e; ++e) {
              if (!opts.empty()) opts += ", ";
              opts += *e;
            }
            err(p.line, p.col, "invalid value for '" + p.name + "' (one of: " + opts + ")");
            continue;
          }
        }
        n.config.push_back({p.name, std::move(cv)});
        continue;
      }
      err(p.line, p.col, "module type '" + m.type + "' has no parameter '" + p.name +
          "' (inputs: " + join_names(info->inputs, [](auto& i) { return i.name; }) + ")");
    }
  }
  add_node("out", dsp::find_module("out"));

  // --- Cables ------------------------------------------------------------
  std::vector<Cable> cables;
  for (const auto& c : ast.cables) {
    if (!c.depth_ident.empty()) {
      err(c.line, c.col, "unknown name '" + c.depth_ident +
          "' for cable depth (params resolve only inside a subpatch)");
      continue;
    }
    int dst;
    std::string dport;
    {
      Resolved rd = resolve(index_of, c.dst.module);
      if (rd.node < 0) { err(c.dst.line, c.dst.col, "unknown module '" + c.dst.module + "'"); continue; }
      dst = rd.node;
      dport = rd.port.empty() ? "in" : rd.port;
    }
    const Node& dn = g->nodes[static_cast<size_t>(dst)];
    int dslot = dn.info->input_index(dport);
    if (dslot < 0) {
      err(c.dst.line, c.dst.col, "module '" + dn.name + "' has no input '" + dport +
          "' (inputs: " + join_names(dn.info->inputs, [](auto& i) { return i.name; }) + ")");
      continue;
    }
    if (c.src.is_literal) {
      g->nodes[static_cast<size_t>(dst)].input_base[static_cast<size_t>(dslot)] +=
          static_cast<float>(c.src.literal * c.depth);
      continue;
    }
    Resolved rs = resolve(index_of, c.src.module);
    if (rs.node < 0) { err(c.src.line, c.src.col, "unknown module '" + c.src.module + "'"); continue; }
    int src = rs.node;
    const Node& sn = g->nodes[static_cast<size_t>(src)];
    // A bare module name means its first output (spec §DSL; e.g. `seq -> ...`
    // reads stepseq's cv, not a port literally named "out").
    std::string sport = rs.port.empty()
        ? (sn.info->outputs.empty() ? "" : sn.info->outputs[0].name)
        : rs.port;
    int sslot = sn.info->output_index(sport);
    if (sslot < 0) {
      err(c.src.line, c.src.col, "module '" + sn.name + "' has no output '" + sport +
          "' (outputs: " + join_names(sn.info->outputs, [](auto& o) { return o.name; }) + ")");
      continue;
    }
    cables.push_back({src, sslot, dst, dslot, static_cast<float>(c.depth)});
  }

  if (!r.errors.empty()) return r;

  // --- Topo order (Kahn; cycles broken at lowest declaration index) ------
  size_t n = g->nodes.size();
  std::vector<int> indegree(n, 0);
  for (const auto& c : cables)
    if (c.src_mod != c.dst_mod) ++indegree[static_cast<size_t>(c.dst_mod)];
  std::vector<int> order;
  std::vector<bool> placed(n, false);
  while (order.size() < n) {
    int pick = -1;
    for (size_t i = 0; i < n; ++i)
      if (!placed[i] && indegree[i] == 0) { pick = static_cast<int>(i); break; }
    if (pick < 0) {  // cycle: force lowest-index remaining node
      for (size_t i = 0; i < n; ++i)
        if (!placed[i]) { pick = static_cast<int>(i); break; }
    }
    placed[static_cast<size_t>(pick)] = true;
    order.push_back(pick);
    for (const auto& c : cables)
      if (c.src_mod == pick && !placed[static_cast<size_t>(c.dst_mod)] && c.src_mod != c.dst_mod)
        --indegree[static_cast<size_t>(c.dst_mod)];
  }

  std::vector<int> new_index(n);
  for (size_t pos = 0; pos < n; ++pos) new_index[static_cast<size_t>(order[pos])] = static_cast<int>(pos);
  std::vector<Node> sorted;
  sorted.reserve(n);
  for (int old : order) sorted.push_back(std::move(g->nodes[static_cast<size_t>(old)]));
  g->nodes = std::move(sorted);
  g->in_cables.assign(n, {});
  for (auto c : cables) {
    c.src_mod = new_index[static_cast<size_t>(c.src_mod)];
    c.dst_mod = new_index[static_cast<size_t>(c.dst_mod)];
    g->in_cables[static_cast<size_t>(c.dst_mod)].push_back(c);
  }
  for (size_t i = 0; i < n; ++i)
    if (g->nodes[i].name == "out") g->out_node = static_cast<int>(i);

  // --- Instantiate -------------------------------------------------------
  for (auto& node : g->nodes) {
    node.instance = node.info->create();
    node.instance->init(sample_rate, node.config);
  }

  r.graph = std::move(g);
  return r;
}

}  // namespace ms::graph
