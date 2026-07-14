#pragma once
#include <memory>
#include <string>
#include <vector>
#include "dsl/ast.h"
#include "dsl/diag.h"
#include "dsp/module.h"

namespace ms::graph {

struct Cable { int src_mod, src_out, dst_mod, dst_in; float depth; };

struct Node {
  std::string name;
  const dsp::ModuleInfo* info;
  std::vector<float> input_base;
  dsp::Config config;
  std::unique_ptr<dsp::Module> instance;
};

struct Graph {
  std::vector<Node> nodes;
  std::vector<std::vector<Cable>> in_cables;
  int out_node = -1;
  double sample_rate = 48000.0;
  // Filled by plan_reload on topology changes: (old_node_idx, new_node_idx)
  // for every module whose (name, type) survives. Consumed on the RT thread
  // by Engine::migrate_from at swap adoption.
  std::vector<std::pair<int, int>> migrate_pairs;
  // Indices of nodes whose ModuleInfo::wants_midi is set. Populated once by
  // Engine's constructor (every new Graph is wrapped in a fresh Engine, on
  // both initial load and hot-reload swap), so it travels with the Graph
  // through the pointer swap instead of living in a separate engine-side
  // vector that could go stale relative to whichever graph is current.
  std::vector<int> midi_nodes;
  // Indices of nodes whose ModuleInfo::wants_audio_in is set. Populated once by
  // Engine's constructor, alongside midi_nodes, for the same reason.
  std::vector<int> audio_in_nodes;
};

struct CompileResult {
  std::unique_ptr<Graph> graph;
  Diags errors;
  bool ok() const { return errors.empty(); }
};

CompileResult compile(const dsl::PatchAst& ast, double sample_rate);

}  // namespace ms::graph
