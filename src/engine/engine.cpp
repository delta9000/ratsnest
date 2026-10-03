#include "engine/engine.h"
#include <algorithm>
#include <cmath>

namespace ms::engine {

Engine::Engine(std::unique_ptr<graph::Graph> g) : g_(std::move(g)) {
  size_t total = 0, max_ins = 1;
  for (size_t ni = 0; ni < g_->nodes.size(); ++ni) {
    const graph::Node& n = g_->nodes[ni];
    out_offset_.push_back(total);
    total += n.info->outputs.size();
    max_ins = std::max(max_ins, n.info->inputs.size());
    if (n.info->wants_midi) g_->midi_nodes.push_back(static_cast<int>(ni));
    if (n.info->wants_audio_in) g_->audio_in_nodes.push_back(static_cast<int>(ni));
  }
  outs_.assign(total, 0.0f);
  ins_.assign(max_ins, 0.0f);
}

void Engine::process(float* left, float* right, size_t frames) {
  const auto& nodes = g_->nodes;
  for (size_t f = 0; f < frames; ++f) {
    step_glides();
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
    left[f]  = outs_[oo];
    right[f] = outs_[oo + 1];
  }
}

void Engine::set_param(size_t node, size_t slot, float target, size_t glide_samples) {
  if (node >= g_->nodes.size()) return;
  auto& base = g_->nodes[node].input_base;
  if (slot >= base.size()) return;
  for (size_t i = 0; i < n_glides_; ++i)
    if (glides_[i].node == node && glides_[i].slot == slot) {
      glides_[i] = glides_[--n_glides_];  // replace in-flight glide on this param
      break;
    }
  if (!g_->nodes[node].info->inputs[slot].glide || glide_samples == 0 || n_glides_ == kMaxGlides) {
    base[slot] = target;
    return;
  }
  glides_[n_glides_++] = Glide{node, slot, target,
      (target - base[slot]) / static_cast<float>(glide_samples), glide_samples};
}

void Engine::step_glides() {
  for (size_t i = 0; i < n_glides_;) {
    Glide& gl = glides_[i];
    float& base = g_->nodes[gl.node].input_base[gl.slot];
    base += gl.step;
    if (--gl.remaining == 0) {
      base = gl.target;                 // land exactly, no float drift
      glides_[i] = glides_[--n_glides_];
    } else {
      ++i;
    }
  }
}

float Engine::output_value(size_t node, size_t slot) const {
  if (node >= g_->nodes.size()) return 0.0f;
  if (slot >= g_->nodes[node].info->outputs.size()) return 0.0f;
  return outs_[out_offset_[node] + slot];
}

size_t Engine::copy_outputs(std::span<float> destination) const {
  const size_t n = std::min(destination.size(), outs_.size());
  std::copy_n(outs_.begin(), n, destination.begin());
  return n;
}

void Engine::deliver_midi_event(const dsp::MidiEvent& e) {
  for (int ni : g_->midi_nodes)
    g_->nodes[static_cast<size_t>(ni)].instance->on_midi(e.data, e.len);
}

void Engine::deliver_audio(const float* samples, size_t n) {
  for (int ni : g_->audio_in_nodes)
    g_->nodes[static_cast<size_t>(ni)].instance->on_audio_in(samples, n);
}

void Engine::migrate_from(const Engine& old) {
  for (auto [oi, ni] : g_->migrate_pairs) {
    if (oi < 0 || ni < 0) continue;
    if (static_cast<size_t>(oi) >= old.g_->nodes.size()) continue;
    if (static_cast<size_t>(ni) >= g_->nodes.size()) continue;
    g_->nodes[static_cast<size_t>(ni)].instance->copy_state_from(
        *old.g_->nodes[static_cast<size_t>(oi)].instance);
  }
}

}  // namespace ms::engine
