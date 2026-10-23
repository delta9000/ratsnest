#pragma once
#include <chrono>
#include <string>
#include <unordered_set>
#include <vector>

namespace ms::io {

class PatchWatcher {
 public:
  explicit PatchWatcher(const std::string& patch_path);
  explicit PatchWatcher(std::vector<std::string> paths);
  ~PatchWatcher();
  PatchWatcher(const PatchWatcher&) = delete;
  PatchWatcher& operator=(const PatchWatcher&) = delete;

  bool ok() const;
  bool poll_changed(int timeout_ms);

 private:
  struct DirWatch {
    int wd = -1;
    std::string dir;
    std::unordered_set<std::string> bases;
  };
  void arm(DirWatch& w);
  void drain();
  bool settled();

  int fd_ = -1;
  std::vector<DirWatch> watches_;
  bool pending_ = false;
  std::chrono::steady_clock::time_point deadline_{};
};

}  // namespace ms::io
