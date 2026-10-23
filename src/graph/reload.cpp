#include "graph/reload.h"

namespace ms::graph {
namespace {

bool same_config_value(const dsp::ConfigValue& a, const dsp::ConfigValue& b) {
  return a.is_num == b.is_num && a.num == b.num && a.str == b.str &&
         a.arr == b.arr && a.strs == b.strs;
}

bool same_config(const dsp::Config& a, const dsp::Config& b) {
  if (a.size() != b.size()) return false;
  for (size_t i = 0; i < a.size(); ++i)
    if (a[i].first != b[i].first || !same_config_value(a[i].second, b[i].second))
      return false;
  return true;
}

bool same_cables(const std::vector<Cable>& a, const std::vector<Cable>& b) {
  if (a.size() != b.size()) return false;
  for (size_t i = 0; i < a.size(); ++i)
    if (a[i].src_mod != b[i].src_mod || a[i].src_out != b[i].src_out ||
        a[i].dst_mod != b[i].dst_mod || a[i].dst_in != b[i].dst_in ||
        a[i].depth != b[i].depth)
      return false;
  return true;
}

// Identical structure: same nodes (name, type, config) in the same order and
// the same cables. Compile is deterministic, so a param-only source edit
// reproduces the old ordering exactly.
bool same_structure(const Graph& a, const Graph& b) {
  if (a.nodes.size() != b.nodes.size()) return false;
  for (size_t i = 0; i < a.nodes.size(); ++i) {
    if (a.nodes[i].name != b.nodes[i].name) return false;
    if (a.nodes[i].info != b.nodes[i].info) return false;
    if (!same_config(a.nodes[i].config, b.nodes[i].config)) return false;
  }
  if (a.in_cables.size() != b.in_cables.size()) return false;
  for (size_t i = 0; i < a.in_cables.size(); ++i)
    if (!same_cables(a.in_cables[i], b.in_cables[i])) return false;
  return true;
}

}  // namespace

ReloadPlan plan_reload(const Graph& old_g, Graph& new_g) {
  ReloadPlan plan;
  if (same_structure(old_g, new_g)) {
    for (size_t n = 0; n < new_g.nodes.size(); ++n)
      for (size_t s = 0; s < new_g.nodes[n].input_base.size(); ++s)
        if (new_g.nodes[n].input_base[s] != old_g.nodes[n].input_base[s])
          plan.params.push_back({static_cast<int>(n), static_cast<int>(s),
                                 new_g.nodes[n].input_base[s]});
    return plan;
  }
  plan.topology = true;
  new_g.migrate_pairs.clear();
  for (size_t ni = 0; ni < new_g.nodes.size(); ++ni)
    for (size_t oi = 0; oi < old_g.nodes.size(); ++oi)
      if (new_g.nodes[ni].name == old_g.nodes[oi].name &&
          new_g.nodes[ni].info == old_g.nodes[oi].info) {
        new_g.migrate_pairs.emplace_back(static_cast<int>(oi), static_cast<int>(ni));
        break;
      }
  return plan;
}

}  // namespace ms::graph
