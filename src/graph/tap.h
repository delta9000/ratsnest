#pragma once
// `render --tap`: capture any endpoint of a patch without editing it. Each
// tap appends a unity `vca` per channel, cabled from the endpoint, to the
// patch text before it is built, so subpatch outputs, voice members and
// default ports resolve exactly as an ordinary cable's source would. The
// capture nodes are sinks: they never feed the mix, so the patch's own
// output is unchanged. Offline only; the render loop reads their outputs.
#include <string>
#include <string_view>
#include <vector>
#include "graph/subs.h"

namespace ms::graph {

struct Tap {
  std::string name;      // the take's name: `<out-stem>.<name>.wav`
  std::string endpoint;  // `node[.port]`, as a cable source is written
  bool stereo = false;   // the endpoint has `l`/`r` or `out_l`/`out_r`
  int node_l = -1, node_r = -1;  // capture nodes in the built graph
};

// `NAME[=node[.port]]`; a bare NAME taps the node of that name.
Tap parse_tap(std::string_view arg);

// Rebuilds `b` (already built from `src`) with each tap's capture nodes and
// fills their node indices. Returns "" on success, or why a tap failed.
std::string add_taps(PatchBuild& b, std::string src, const std::string& patch_path,
                     double rate, std::vector<Tap>& taps);

}  // namespace ms::graph
