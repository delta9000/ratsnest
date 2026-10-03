#pragma once
#include "engine/controller.h"
#include "io/watcher.h"
#include <nlohmann/json.hpp>

namespace ms::studio {
using Json = nlohmann::json;
// UI and local RPC calls run on the same non-RT thread. Only RtRunner crosses
// into the audio thread; graph edits and filesystem operations never do.
class Session {
public:
  Session(std::string path, double rate = 48000);
  engine::RtRunner &runner() { return runner_; }
  const graph::Graph *graph() const { return controller_.graph(); }
  const std::string &source() const { return source_; }
  const std::string &path() const { return path_; }
  const std::string &message() const { return message_; }
  uint64_t revision() const { return revision_; }
  bool dirty() const { return source_ != disk_source_; }
  void poll();
  void set_audio_status(Json status) { audio_status_ = std::move(status); }
  Json state() const;
  Json call(const std::string &method, const Json &args);
  static Json interfaces();

private:
  Json apply(const std::string &text, uint64_t expected, bool record = true);
  Json set_param(const Json &args);
  Json save(uint64_t expected);
  Json remove(const Json &args, bool node);
  void require_revision(uint64_t expected) const;
  std::string source_, disk_source_, path_, message_;
  std::vector<std::string> undo_, redo_;
  Json audio_status_ = {{"mode", "initializing"}};
  uint64_t revision_ = 0;
  engine::RtRunner runner_;
  engine::Controller controller_;
  std::unique_ptr<io::PatchWatcher> watcher_;
  engine::Observation observation_;
};
} // namespace ms::studio
