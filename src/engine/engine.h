#pragma once
#include <memory>
#include <vector>
#include "graph/graph.h"

namespace ms::engine {

class Engine {
 public:
  explicit Engine(std::unique_ptr<graph::Graph> g);
  void process(float* left, float* right, size_t frames);
  double sample_rate() const { return g_->sample_rate; }

 private:
  std::unique_ptr<graph::Graph> g_;
  std::vector<float> outs_;        // flat persistent [node][out_slot]
  std::vector<size_t> out_offset_; // per node
  std::vector<float> ins_;         // scratch, sized to max input count
};

}  // namespace ms::engine
