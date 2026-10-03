#pragma once
#include <memory>
#include <string>
#include <string_view>
#include "dsl/diag.h"
#include "engine/rt_runner.h"
#include "graph/graph.h"

namespace ms::engine {

// Non-RT companion of RtRunner: parse -> compile -> diff -> commands.
// Owns a "shadow" compile of the last good source; all reload planning diffs
// against the shadow, never against the live graph (race-free by design).
class Controller {
 public:
  Controller(RtRunner& runner, double sample_rate);

  struct LoadResult {
    bool ok = false;
    bool topology = false;
    Diags errors;
    std::vector<std::string> files;
  };

  LoadResult load(std::string_view src, const std::string& patch_path = {});
  bool set_watch(std::string_view spec);
  void clear_watch();
  void drain_retired();
  const graph::Graph* graph() const { return shadow_.get(); }
  uint64_t generation() const { return generation_; }
  int engines_in_flight() const { return pushed_ - reaped_; }

 private:
  bool push_watch_for_shadow();  // resolve watch_spec_ against shadow_, push

  RtRunner& runner_;
  double rate_;
  std::unique_ptr<graph::Graph> shadow_;
  std::string watch_spec_;
  uint64_t generation_ = 0;
  int pushed_ = 0;  // engines handed to the runner
  int reaped_ = 0;  // engines retired back and deleted
};

}  // namespace ms::engine
