#pragma once
// `render --tap`: capture any endpoint of a patch without editing it. Each
// tap appends a unity `vca` per channel (one for mono, two for stereo),
// cabled from the endpoint, to the patch text, which is then built once, so subpatch outputs, voice members and
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
  std::vector<int> nodes;  // capture nodes in the built graph: l, then r if stereo
};

// `NAME[=node[.port]]`; a bare NAME taps the node of that name.
Tap parse_tap(std::string_view arg);

// Why the taps cannot be written ("" if they can): a name that is empty,
// repeated, or not a plain file-name part (no '/', backslash or '..'), or an
// empty endpoint.
std::string check_taps(const std::vector<Tap>& taps);

// Rebuilds `b` (already built from `src`) once with every tap's capture
// nodes, choosing mono or stereo from the endpoint's ports in `b`, and fills
// their node indices. Returns "" on success, or why a tap failed (leaving
// `b` as it was).
std::string add_taps(PatchBuild& b, std::string src, const std::string& patch_path,
                     double rate, std::vector<Tap>& taps);

}  // namespace ms::graph
