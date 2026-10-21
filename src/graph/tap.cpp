#include "graph/tap.h"
#include <algorithm>

namespace ms::graph {
namespace {

// Capture node names: `__tap<i>_l`, `__tap<i>_r`.
std::string node(size_t i, const char* side) { return "__tap" + std::to_string(i) + side; }

// Patch lines for one capture channel: a unity vca reading `source`.
std::string capture(const std::string& name, const std::string& source) {
  return "\n" + name + " = vca {}\n" + source + " -> " + name + ".in\n";
}

// A capture node's index; add_taps built it, so it is there.
int index_of(const Graph& g, const std::string& name) {
  const auto it = std::find_if(g.nodes.begin(), g.nodes.end(),
                               [&](const Node& n) { return n.name == name; });
  return static_cast<int>(it - g.nodes.begin());
}

}  // namespace

// Without '=', find returns npos and npos + 1 wraps to 0: the endpoint is
// the whole argument, as is the name.
Tap parse_tap(std::string_view arg) {
  const size_t eq = arg.find('=');
  return {std::string(arg.substr(0, eq)), std::string(arg.substr(eq + 1))};
}

std::string add_taps(PatchBuild& b, std::string src, const std::string& patch_path,
                     double rate, std::vector<Tap>& taps) {
  // Stereo first: the first port pair the endpoint has, else its own port.
  constexpr const char* kPairs[][2] = {{".l", ".r"}, {".out_l", ".out_r"}, {"", ""}};
  for (size_t i = 0; i < taps.size(); ++i) {
    Tap& t = taps[i];
    for (const auto& [l, r] : kPairs) {
      t.stereo = *l != '\0';
      const std::string text = capture(node(i, "_l"), t.endpoint + l) +
                               capture(node(i, "_r"), t.endpoint + r);
      b = build_patch(src + text, patch_path, rate);
      if (b.compiled.ok()) {
        src += text;
        break;
      }
    }
    if (!b.compiled.ok()) return "--tap " + t.name + "=" + t.endpoint + ": " + b.compiled.errors[0].msg;
  }
  for (size_t i = 0; i < taps.size(); ++i) {
    taps[i].node_l = index_of(*b.compiled.graph, node(i, "_l"));
    taps[i].node_r = index_of(*b.compiled.graph, node(i, "_r"));
  }
  return "";
}

}  // namespace ms::graph
