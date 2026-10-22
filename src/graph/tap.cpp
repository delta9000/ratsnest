#include "graph/tap.h"
#include <algorithm>

namespace ms::graph {
namespace {

// Capture node names: `__tap<i>_l`, and `__tap<i>_r` for a stereo tap.
std::string node(size_t i, size_t channel) { return "__tap" + std::to_string(i) + "_" + "lr"[channel]; }

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

// What to capture for `endpoint`: its `l`/`r` (else `out_l`/`out_r`) pair
// when the outputs it exposes in the patch as built have one (a sub
// instance's declared outputs, or a module's), else the endpoint itself.
std::vector<std::string> channels(const PatchBuild& b, const std::string& endpoint) {
  std::vector<std::string> ports;
  for (const auto& [inst, outs] : b.sub_outputs)
    if (inst == endpoint) ports = outs;
  for (const Node& n : b.compiled.graph->nodes)
    if (n.name == endpoint)
      for (const auto& o : n.info->outputs) ports.emplace_back(o.name);
  const auto has = [&](const std::string& p) { return std::find(ports.begin(), ports.end(), p) != ports.end(); };
  for (const std::string side : {"", "out_"})
    if (has(side + "l") && has(side + "r")) return {endpoint + "." + side + "l", endpoint + "." + side + "r"};
  return {endpoint};
}

size_t lines(const std::string& s) { return static_cast<size_t>(std::count(s.begin(), s.end(), '\n')); }

}  // namespace

// Without '=', find returns npos and npos + 1 wraps to 0: the endpoint is
// the whole argument, as is the name.
Tap parse_tap(std::string_view arg) {
  const size_t eq = arg.find('=');
  return {std::string(arg.substr(0, eq)), std::string(arg.substr(eq + 1))};
}

std::string check_taps(const std::vector<Tap>& taps) {
  for (size_t i = 0; i < taps.size(); ++i) {
    const std::string& n = taps[i].name;
    if (n.empty()) return "--tap needs a name: --tap NAME[=node[.port]]";
    if (n.find_first_of("/\\") != std::string::npos || n.find("..") != std::string::npos)
      return "--tap name '" + n + "' may not contain '/', '\\' or '..' (it names a file beside the take)";
    if (taps[i].endpoint.empty()) return "--tap " + n + "= needs an endpoint after '='";
    if (std::any_of(taps.begin(), taps.begin() + static_cast<long>(i), [&](const Tap& t) { return t.name == n; }))
      return "--tap name '" + n + "' is given twice; each tap writes its own file";
  }
  return "";
}

std::string add_taps(PatchBuild& b, std::string src, const std::string& patch_path,
                     double rate, std::vector<Tap>& taps) {
  if (taps.empty()) return "";
  std::vector<size_t> first_line;  // each tap's first capture line, 1-based
  for (size_t i = 0; i < taps.size(); ++i) {
    const std::vector<std::string> ch = channels(b, taps[i].endpoint);
    taps[i].stereo = ch.size() == 2;
    first_line.push_back(lines(src) + 2);
    for (size_t c = 0; c < ch.size(); ++c) src += capture(node(i, c), ch[c]);
  }
  PatchBuild tapped = build_patch(src, patch_path, rate);
  if (!tapped.compiled.ok()) {
    // The patch built alone, so the error is in the last tap starting at or
    // before its line (the first tap also owns any line before it).
    const ms::Diag& d = tapped.compiled.errors[0];
    first_line[0] = 0;
    const auto line = static_cast<size_t>(std::max(d.line, 0));
    const auto i = static_cast<size_t>(std::upper_bound(first_line.begin(), first_line.end(), line) -
                                       first_line.begin() - 1);
    return "--tap " + taps[i].name + "=" + taps[i].endpoint + ": " + d.msg;
  }
  b = std::move(tapped);
  for (size_t i = 0; i < taps.size(); ++i) {
    taps[i].nodes.clear();
    for (size_t c = 0; c < 1u + taps[i].stereo; ++c) taps[i].nodes.push_back(index_of(*b.compiled.graph, node(i, c)));
  }
  return "";
}

}  // namespace ms::graph
