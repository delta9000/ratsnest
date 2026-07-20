#include "engine/controller.h"
#include <filesystem>
#include "dsl/parser.h"
#include "graph/reload.h"
#include "graph/subs.h"
#include "graph/voices.h"

namespace ms::engine {

Controller::Controller(RtRunner& runner, double sample_rate)
    : runner_(runner), rate_(sample_rate) {}

Controller::LoadResult Controller::load(std::string_view src, const std::string& patch_path) {
  LoadResult res;
  drain_retired();

  auto p = dsl::parse(src);
  if (!p.ok()) {
    res.errors = std::move(p.errors);
    return res;
  }

  std::string dir = ".";
  if (!patch_path.empty()) {
    std::filesystem::path pp(patch_path);
    if (pp.has_parent_path()) dir = pp.parent_path().string();
  }
  auto sx = graph::expand_subs(p.ast, dir, graph::read_text_file, res.errors);
  if (!res.errors.empty()) return res;
  res.files = std::move(sx.files);

  auto c = graph::compile(p.ast, rate_);
  if (!c.ok()) {
    res.errors = std::move(c.errors);
    return res;
  }

  if (shadow_) {
    auto plan = graph::plan_reload(*shadow_, *c.graph);
    if (!plan.topology) {
      for (const auto& pc : plan.params) {
        Command cmd;
        cmd.kind = Command::Kind::SetParam;
        cmd.node = static_cast<uint32_t>(pc.node);
        cmd.slot = static_cast<uint32_t>(pc.slot);
        cmd.value = pc.target;
        if (!runner_.push(cmd)) break;  // full queue: next save re-syncs
      }
      shadow_ = std::move(c.graph);  // same structure, fresh bases
      res.ok = true;
      return res;
    }
  }

  // Topology path (or first load). Gate: keep the retire queue unfillable.
  if (engines_in_flight() >= 2) {
    res.errors.push_back({0, 0, "reload still in flight; save again in a moment", {}});
    return res;
  }

  // The RT engine and the shadow must be distinct objects: compile again.
  auto c2 = graph::compile(p.ast, rate_);
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
  if (!watch_spec_.empty()) push_watch_for_shadow();
  return res;
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
  if (!shadow_ || watch_spec_.empty()) return false;
  // Node names may themselves contain dots (voice members: `lead.osc`,
  // instanced as `lead.osc#0`), so the watch spec must be split by the same
  // longest-declared-name rule as ordinary endpoint resolution, not by its
  // first dot.
  auto is_node_name = [&](const std::string& n) {
    for (const auto& node : shadow_->nodes)
      if (node.name == n) return true;
    return false;
  };
  graph::SplitEndpoint s = graph::split_endpoint(is_node_name, watch_spec_);
  if (!s.ok) return false;
  for (size_t i = 0; i < shadow_->nodes.size(); ++i) {
    const auto& n = shadow_->nodes[i];
    if (n.name != s.module) continue;
    int slot = 0;
    if (!s.port.empty()) {
      slot = n.info->output_index(s.port);
      if (slot < 0) return false;
    }
    Command cmd;
    cmd.kind = Command::Kind::Watch;
    cmd.node = static_cast<uint32_t>(i);
    cmd.slot = static_cast<uint32_t>(slot);
    return runner_.push(cmd);
  }
  return false;
}

void Controller::drain_retired() {
  Engine* e = nullptr;
  while (runner_.try_retire(e)) {
    delete e;
    ++reaped_;
  }
}

}  // namespace ms::engine
