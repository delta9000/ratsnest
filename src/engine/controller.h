#pragma once
#include <memory>
#include <optional>
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
    bool deferred = false;  // valid topology edit held until a fade finishes
    Diags errors;
    std::vector<std::string> files;
  };

  LoadResult load(std::string_view src, const std::string& patch_path = {});
  // A topology edit that arrives while a reload is still crossfading is kept,
  // newest only (any later load() supersedes it), and applied here once the
  // fading engine has been reaped. nullopt: nothing pending, or still fading.
  std::optional<LoadResult> apply_pending();
  bool has_pending() const { return pending_; }
  bool set_watch(std::string_view spec);
  void clear_watch();
  bool watch_available() const { return watch_available_; }
  void drain_retired();
  const graph::Graph* graph() const { return shadow_.get(); }
  int engines_in_flight() const { return pushed_ - reaped_; }

 private:
  static bool resolve_watch(const graph::Graph& g, std::string_view spec, Command& cmd);
  bool watch_available_ = false;

  RtRunner& runner_;
  double rate_;
  std::unique_ptr<graph::Graph> shadow_;
  std::string watch_spec_;
  int pushed_ = 0;  // engines handed to the runner
  int reaped_ = 0;  // engines retired back and deleted
  bool pending_ = false;
  std::string pending_src_, pending_path_;
};

}  // namespace ms::engine
