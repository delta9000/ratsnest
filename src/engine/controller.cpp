#include "engine/controller.h"
#include "dsl/parser.h"
#include "graph/reload.h"

namespace ms::engine {

Controller::Controller(RtRunner& runner, double sample_rate)
    : runner_(runner), rate_(sample_rate) {}

Controller::LoadResult Controller::load(std::string_view src) {
  LoadResult res;
  drain_retired();

  auto p = dsl::parse(src);
  if (!p.ok()) {
    res.errors = std::move(p.errors);
    return res;
  }
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
    res.errors.push_back({0, 0, "reload still in flight; save again in a moment"});
    return res;
  }

  // The RT engine and the shadow must be distinct objects: compile again.
  auto c2 = graph::compile(p.ast, rate_);
  Command cmd;
  cmd.kind = Command::Kind::Swap;
  cmd.engine = new Engine(std::move(c.graph));
  if (!runner_.push(cmd)) {
    delete cmd.engine;
    res.errors.push_back({0, 0, "engine command queue full; try again"});
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
  std::string_view spec = watch_spec_;
  std::string_view mod = spec, port;
  if (auto dot = spec.find('.'); dot != std::string_view::npos) {
    mod = spec.substr(0, dot);
    port = spec.substr(dot + 1);
  }
  for (size_t i = 0; i < shadow_->nodes.size(); ++i) {
    const auto& n = shadow_->nodes[i];
    if (n.name != mod) continue;
    int slot = 0;
    if (!port.empty()) {
      slot = n.info->output_index(port);
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
