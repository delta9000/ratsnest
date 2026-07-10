#pragma once
#include <vector>
#include "graph/graph.h"

namespace ms::graph {

struct ParamChange { int node; int slot; float target; };

struct ReloadPlan {
  bool topology = false;
  std::vector<ParamChange> params;  // meaningful when topology == false
};

// Diff the shadow of the playing graph against a freshly compiled one.
// Param-only edits become glide targets; anything structural becomes a swap
// plan with migration pairs written into new_g.migrate_pairs.
ReloadPlan plan_reload(const Graph& old_g, Graph& new_g);

}  // namespace ms::graph
