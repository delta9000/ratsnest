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
};

struct CompileResult {
  std::unique_ptr<Graph> graph;
  Diags errors;
  bool ok() const { return errors.empty(); }
};

CompileResult compile(const dsl::PatchAst& ast, double sample_rate);

}  // namespace ms::graph
