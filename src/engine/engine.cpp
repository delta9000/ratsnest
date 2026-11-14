#include "engine/engine.h"
#include <algorithm>
#include <cmath>

namespace ms::engine {
namespace {
float finite_or_zero(float v) { return std::isfinite(v) ? v : 0.0f; }
}  // namespace

Engine::Engine(std::unique_ptr<graph::Graph> g) : g_(std::move(g)) {
  size_t total = 0, total_ins = 0, n_cables = 0;
  for (size_t ni = 0; ni < g_->nodes.size(); ++ni) {
    const graph::Node& n = g_->nodes[ni];
    out_offset_.push_back(total);
    in_offset_.push_back(total_ins);
    total += n.info->outputs.size();
    total_ins += n.info->inputs.size();
    n_cables += g_->in_cables[ni].size();
    if (n.info->wants_midi) g_->midi_nodes.push_back(static_cast<int>(ni));
    if (n.info->wants_audio_in) g_->audio_in_nodes.push_back(static_cast<int>(ni));
  }
  outs_.assign(total, 0.0f);
  // Only outputs the graph reads get the per-sample finite guard: cable
  // sources and the out node's pair. An unread output feeds nothing, and
  // output_value/copy_outputs guard it when someone looks.
  std::vector<bool> read(total, false);
  for (const auto& into : g_->in_cables)
    for (const graph::Cable& c : into)
      read[out_offset_[static_cast<size_t>(c.src_mod)] + static_cast<size_t>(c.src_out)] = true;
  ins_.assign(std::max<size_t>(total_ins, 1), 0.0f);  // never an empty slice

  // Flatten the graph into one contiguous schedule so the per-sample loop
  // touches no ModuleInfo, no nested vectors and no offset lookups. The graph
  // is immutable once wrapped (param changes write input_base in place, never
  // resize it), so these pointers stay valid for the Engine's lifetime.
  cables_.reserve(n_cables);
  steps_.reserve(g_->nodes.size());
  for (size_t ni = 0; ni < g_->nodes.size(); ++ni) {
    graph::Node& n = g_->nodes[ni];
    float* const ins = ins_.data() + in_offset_[ni];
    std::copy(n.input_base.begin(), n.input_base.end(), ins);
    Step st;
    st.mod = n.instance.get();
    st.ins = ins;
    st.outs = outs_.data() + out_offset_[ni];
    st.guard_begin = guarded_.size();
    for (size_t o = 0; o < n.info->outputs.size(); ++o)
      if (read[out_offset_[ni] + o] || static_cast<int>(ni) == g_->out_node)
        guarded_.push_back(st.outs + o);
    st.guard_end = guarded_.size();
    st.cable_begin = cables_.size();
    for (const graph::Cable& c : g_->in_cables[ni]) {
      float* const dst = ins + static_cast<size_t>(c.dst_in);
      const bool first = std::none_of(
          cables_.begin() + static_cast<std::ptrdiff_t>(st.cable_begin), cables_.end(),
          [dst](const FlatCable& fc) { return fc.dst == dst; });
      cables_.push_back(FlatCable{dst,
          first ? n.input_base.data() + c.dst_in : dst,
          outs_.data() + out_offset_[static_cast<size_t>(c.src_mod)] + static_cast<size_t>(c.src_out),
          c.depth});
    }
    st.cable_end = cables_.size();
    steps_.push_back(st);
  }
  out_index_ = out_offset_[static_cast<size_t>(g_->out_node)];
}

void Engine::process(float* left, float* right, size_t frames) {
  const float* const outs = outs_.data();
  const FlatCable* const cables = cables_.data();
  float* const* const guarded = guarded_.data();
  for (size_t f = 0; f < frames; ++f) {
    if (n_glides_ != 0) step_glides();
    for (const Step& st : steps_) {
      for (size_t c = st.cable_begin; c < st.cable_end; ++c)
        *cables[c].dst = *cables[c].init + *cables[c].src * cables[c].depth;
      st.mod->process(st.ins, st.outs);
      for (size_t o = st.guard_begin; o < st.guard_end; ++o)
        if (!std::isfinite(*guarded[o])) *guarded[o] = 0.0f;
    }
    left[f]  = outs[out_index_];
    right[f] = outs[out_index_ + 1];
  }
}

void Engine::write_base(size_t node, size_t slot, float v) {
  g_->nodes[node].input_base[slot] = v;
  ins_[in_offset_[node] + slot] = v;  // rebuilt every sample if cabled
}

void Engine::set_param(size_t node, size_t slot, float target, size_t glide_samples) {
  if (node >= g_->nodes.size()) return;
  const auto& base = g_->nodes[node].input_base;
  if (slot >= base.size()) return;
  for (size_t i = 0; i < n_glides_; ++i)
    if (glides_[i].node == node && glides_[i].slot == slot) {
      glides_[i] = glides_[--n_glides_];  // replace in-flight glide on this param
      break;
    }
  if (!g_->nodes[node].info->inputs[slot].glide || glide_samples == 0 || n_glides_ == kMaxGlides) {
    write_base(node, slot, target);
    return;
  }
  glides_[n_glides_++] = Glide{node, slot, target,
      (target - base[slot]) / static_cast<float>(glide_samples), glide_samples};
}

void Engine::step_glides() {
  for (size_t i = 0; i < n_glides_;) {
    Glide& gl = glides_[i];
    if (--gl.remaining == 0) {
      write_base(gl.node, gl.slot, gl.target);  // land exactly, no float drift
      glides_[i] = glides_[--n_glides_];
    } else {
      write_base(gl.node, gl.slot, g_->nodes[gl.node].input_base[gl.slot] + gl.step);
      ++i;
    }
  }
}

float Engine::output_value(size_t node, size_t slot) const {
  if (node >= g_->nodes.size()) return 0.0f;
  if (slot >= g_->nodes[node].info->outputs.size()) return 0.0f;
  return finite_or_zero(outs_[out_offset_[node] + slot]);
}

size_t Engine::copy_outputs(std::span<float> destination) const {
  const size_t n = std::min(destination.size(), outs_.size());
  std::transform(outs_.begin(), outs_.begin() + static_cast<std::ptrdiff_t>(n),
                 destination.begin(), finite_or_zero);
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
