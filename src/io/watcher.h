#pragma once
#include <chrono>
#include <string>

namespace ms::io {

// Watches one patch file for saves, robust against editor temp-file+rename
// saves: watches the parent directory and matches the basename. Debounces
// with a 50ms quiescence window (see docs/research hotreload brief).
class PatchWatcher {
 public:
  explicit PatchWatcher(const std::string& patch_path);
  ~PatchWatcher();
  PatchWatcher(const PatchWatcher&) = delete;
  PatchWatcher& operator=(const PatchWatcher&) = delete;

  bool ok() const { return fd_ >= 0 && wd_ >= 0; }
  bool poll_changed(int timeout_ms);

 private:
  void arm();
  void drain();  // read all currently-available inotify events (non-blocking)

  int fd_ = -1;
  int wd_ = -1;
  std::string dir_, base_;
  bool pending_ = false;
  std::chrono::steady_clock::time_point deadline_{};
};

}  // namespace ms::io
