#pragma once
#include <array>
#include <memory>
#include <vector>
#include "graph/graph.h"

namespace ms::engine {

class Engine {
 public:
  explicit Engine(std::unique_ptr<graph::Graph> g);
  void process(float* left, float* right, size_t frames);
  double sample_rate() const { return g_->sample_rate; }

  // RT-safe param glide: linear ramp to target over glide_samples (0 = jump).
  void set_param(size_t node, size_t slot, float target, size_t glide_samples);
  // Last processed sample's value on an output port; 0.0f if out of range.
  float output_value(size_t node, size_t slot) const;
  const graph::Graph& graph() const { return *g_; }
  // Copy migratable module state from the engine being replaced, following
  // graph().migrate_pairs. RT-called at swap adoption, when the RT thread
  // owns both engines: race-free and allocation-free by construction.
  void migrate_from(const Engine& old);

 private:
  void step_glides();

  struct Glide {
    size_t node = 0, slot = 0;
    float target = 0.0f, step = 0.0f;
    size_t remaining = 0;
  };
  static constexpr size_t kMaxGlides = 64;

  std::unique_ptr<graph::Graph> g_;
  std::vector<float> outs_;        // flat persistent [node][out_slot]
  std::vector<size_t> out_offset_; // per node
  std::vector<float> ins_;         // scratch, sized to max input count
  std::array<Glide, kMaxGlides> glides_{};
  size_t n_glides_ = 0;
};

}  // namespace ms::engine
