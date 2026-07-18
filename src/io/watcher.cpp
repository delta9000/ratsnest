#include "io/watcher.h"

#include <poll.h>
#include <sys/inotify.h>
#include <unistd.h>

#include <climits>
#include <filesystem>

namespace ms::io {

namespace {
constexpr auto kDebounce = std::chrono::milliseconds(50);
constexpr uint32_t kDirMask =
    IN_CLOSE_WRITE | IN_MOVED_TO | IN_MOVE_SELF | IN_DELETE_SELF;
}  // namespace

PatchWatcher::PatchWatcher(const std::string& patch_path)
    : PatchWatcher(std::vector<std::string>{patch_path}) {}

PatchWatcher::PatchWatcher(std::vector<std::string> paths) {
  fd_ = inotify_init1(IN_NONBLOCK | IN_CLOEXEC);
  for (const auto& path : paths) {
    std::filesystem::path p(path);
    const std::string dir = p.has_parent_path() ? p.parent_path().string() : ".";
    const std::string base = p.filename().string();
    DirWatch* w = nullptr;
    for (auto& existing : watches_)
      if (existing.dir == dir) w = &existing;
    if (!w) {
      watches_.push_back({-1, dir, {}});
      w = &watches_.back();
    }
    w->bases.insert(base);
  }
  if (fd_ >= 0)
    for (auto& w : watches_) arm(w);
}

PatchWatcher::~PatchWatcher() {
  if (fd_ >= 0) close(fd_);
}

bool PatchWatcher::ok() const {
  if (fd_ < 0 || watches_.empty()) return false;
  for (const auto& w : watches_)
    if (w.wd < 0) return false;
  return true;
}

void PatchWatcher::arm(DirWatch& w) {
  if (w.wd >= 0) inotify_rm_watch(fd_, w.wd);
  w.wd = inotify_add_watch(fd_, w.dir.c_str(), kDirMask);
}

void PatchWatcher::drain() {
  alignas(inotify_event) char buf[sizeof(inotify_event) + NAME_MAX + 1];
  ssize_t n = 0;
  while ((n = read(fd_, buf, sizeof(buf))) > 0) {
    for (char* ptr = buf; ptr < buf + n;) {
      const auto* ev = reinterpret_cast<const inotify_event*>(ptr);
      for (auto& w : watches_) {
        if (ev->wd != w.wd) continue;
        if (ev->mask & (IN_MOVE_SELF | IN_DELETE_SELF)) {
          arm(w);
        } else if ((ev->mask & (IN_CLOSE_WRITE | IN_MOVED_TO)) && ev->len > 0 &&
                   w.bases.count(ev->name)) {
          pending_ = true;
          deadline_ = std::chrono::steady_clock::now() + kDebounce;
        }
        break;
      }
      ptr += sizeof(inotify_event) + ev->len;
    }
  }
}

bool PatchWatcher::poll_changed(int timeout_ms) {
  if (!ok()) return false;

  drain();

  if (pending_ && std::chrono::steady_clock::now() >= deadline_) {
    pending_ = false;
    return true;
  }

  int wait_ms = timeout_ms;
  if (pending_) {
    const auto until = std::chrono::duration_cast<std::chrono::milliseconds>(
        deadline_ - std::chrono::steady_clock::now()).count();
    wait_ms = static_cast<int>(std::min(static_cast<long>(wait_ms), std::max(0L, until)));
  }

  pollfd pfd{fd_, POLLIN, 0};
  if (poll(&pfd, 1, wait_ms) > 0 && (pfd.revents & POLLIN)) {
    drain();
  }

  if (pending_ && std::chrono::steady_clock::now() >= deadline_) {
    pending_ = false;
    return true;
  }
  return false;
}

}  // namespace ms::io
