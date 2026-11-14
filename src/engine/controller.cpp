#include "engine/controller.h"
#include "graph/reload.h"
#include "graph/subs.h"
#include "graph/voices.h"

namespace ms::engine {

Controller::Controller(RtRunner& runner, double sample_rate)
    : runner_(runner), rate_(sample_rate) {}

Controller::LoadResult Controller::load(std::string_view src, const std::string& patch_path) {
  LoadResult res;
  drain_retired();
  pending_ = false;  // this edit is newer than anything held back

  auto b = graph::build_patch(src, patch_path, rate_);
  auto& c = b.compiled;
  res.files = std::move(b.files);
  if (!c.ok()) {
    res.errors = std::move(c.errors);
    return res;
  }

  if (shadow_) {
    auto plan = graph::plan_reload(*shadow_, *c.graph);
    if (!plan.topology) {
      if (plan.params.size() > runner_.command_capacity()) {
        res.errors.push_back({0, 0, "parameter command queue full; retry the edit", {}});
        return res;
      }
      for (const auto& pc : plan.params) {
        Command cmd;
        cmd.kind = Command::Kind::SetParam;
        cmd.node = static_cast<uint32_t>(pc.node);
        cmd.slot = static_cast<uint32_t>(pc.slot);
        cmd.value = pc.target;
        runner_.push(cmd);  // capacity checked for the whole batch above
      }
      shadow_ = std::move(c.graph);  // same structure, fresh bases
      res.ok = true;
      return res;
    }
  }

  // Topology path (or first load). Gate: keep the retire queue unfillable.
  // A save during the crossfade is held (newest wins) for apply_pending().
  if (engines_in_flight() >= 2) {
    pending_ = true;
    pending_src_ = std::string(src);
    pending_path_ = patch_path;
    res.deferred = true;
    return res;
  }

  // The RT engine and the shadow must be distinct objects: compile again.
  auto c2 = graph::compile(b.ast, rate_);
  Command cmd;
  cmd.kind = Command::Kind::Swap;
  cmd.engine = new Engine(std::move(c.graph));
  if (!runner_.push(cmd)) {
    delete cmd.engine;
    res.errors.push_back({0, 0, "engine command queue full; try again", {}});
    return res;
  }
  ++pushed_;
  shadow_ = std::move(c2.graph);
  res.ok = true;
  res.topology = true;
  push_watch_for_shadow();  // no-op without a watch spec
  return res;
}

std::optional<Controller::LoadResult> Controller::apply_pending() {
  drain_retired();
  if (!pending_ || engines_in_flight() >= 2) return std::nullopt;
  const std::string src = std::move(pending_src_);
  const std::string path = std::move(pending_path_);
  return load(src, path);
}

bool Controller::set_watch(std::string_view spec) {
  std::string keep = watch_spec_;
  watch_spec_ = std::string(spec);
  if (push_watch_for_shadow()) return true;
  watch_spec_ = std::move(keep);
  return false;
}

void Controller::clear_watch() {
  watch_spec_.clear();
  Command cmd;
  cmd.kind = Command::Kind::Watch;
  cmd.node = RtRunner::kWatchOff;
  runner_.push(cmd);
}

bool Controller::push_watch_for_shadow() {
  if (!shadow_) return false;
  // Node names may themselves contain dots (voice members: `lead.osc`,
  // instanced as `lead.osc#0`), so the watch spec must be split by the same
  // longest-declared-name rule as ordinary endpoint resolution, not by its
  // first dot. A failed split (or empty spec) yields module "", never a node.
  auto node_index = [&](const std::string& n) {
    for (size_t i = 0; i < shadow_->nodes.size(); ++i)
      if (shadow_->nodes[i].name == n) return static_cast<int>(i);
    return -1;
  };
  const graph::SplitEndpoint s = graph::split_endpoint(
      [&](const std::string& n) { return node_index(n) >= 0; }, watch_spec_);
  const int node = node_index(s.module);
  if (node < 0) return false;
  const int slot = s.port.empty() ? 0 : shadow_->nodes[node].info->output_index(s.port);
  if (slot < 0) return false;
  Command cmd;
  cmd.kind = Command::Kind::Watch;
  cmd.node = static_cast<uint32_t>(node);
  cmd.slot = static_cast<uint32_t>(slot);
  return runner_.push(cmd);
}

void Controller::drain_retired() {
  Engine* e = nullptr;
  while (runner_.try_retire(e)) {
    delete e;
    ++reaped_;
  }
}

}  // namespace ms::engine
