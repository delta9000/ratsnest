#include "engine/engine.h"
#include <algorithm>
#include <cmath>

namespace ms::engine {

Engine::Engine(std::unique_ptr<graph::Graph> g) : g_(std::move(g)) {
  size_t total = 0, max_ins = 1;
  for (const auto& n : g_->nodes) {
    out_offset_.push_back(total);
    total += n.info->outputs.size();
    max_ins = std::max(max_ins, n.info->inputs.size());
  }
  outs_.assign(total, 0.0f);
  ins_.assign(max_ins, 0.0f);
}

void Engine::process(float* left, float* right, size_t frames) {
  const auto& nodes = g_->nodes;
  for (size_t f = 0; f < frames; ++f) {
    for (size_t ni = 0; ni < nodes.size(); ++ni) {
      const graph::Node& n = nodes[ni];
      const size_t n_ins = n.info->inputs.size();
      for (size_t s = 0; s < n_ins; ++s) ins_[s] = n.input_base[s];
      for (const graph::Cable& c : g_->in_cables[ni])
        ins_[static_cast<size_t>(c.dst_in)] +=
            outs_[out_offset_[static_cast<size_t>(c.src_mod)] + static_cast<size_t>(c.src_out)] * c.depth;
      float* node_outs = &outs_[out_offset_[ni]];
      n.instance->process(ins_.data(), node_outs);
      for (size_t o = 0; o < n.info->outputs.size(); ++o)
        if (!std::isfinite(node_outs[o])) node_outs[o] = 0.0f;
    }
    const size_t oo = out_offset_[static_cast<size_t>(g_->out_node)];
    left[f]  = std::clamp(outs_[oo],     -1.0f, 1.0f);
    right[f] = std::clamp(outs_[oo + 1], -1.0f, 1.0f);
  }
}

}  // namespace ms::engine
