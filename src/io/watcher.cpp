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

PatchWatcher::PatchWatcher(const std::string& patch_path) {
  std::filesystem::path p(patch_path);
  dir_ = p.has_parent_path() ? p.parent_path().string() : ".";
  base_ = p.filename().string();
  fd_ = inotify_init1(IN_NONBLOCK | IN_CLOEXEC);
  if (fd_ >= 0) arm();
}

void PatchWatcher::arm() {
  if (wd_ >= 0) {
    // inotify is inode-based: IN_MOVE_SELF/IN_DELETE_SELF don't remove the
    // kernel-side watch on their own. Drop it explicitly before re-adding so
    // a directory rename doesn't leave a second live watch on this fd.
    // Return value ignored: for IN_DELETE_SELF the watch may already be gone.
    inotify_rm_watch(fd_, wd_);
  }
  wd_ = inotify_add_watch(fd_, dir_.c_str(), kDirMask);
}

PatchWatcher::~PatchWatcher() {
  if (fd_ >= 0) close(fd_);
}

void PatchWatcher::drain() {
  // NOLINTNEXTLINE(cppcoreguidelines-avoid-c-arrays): kernel ABI buffer
  alignas(inotify_event) char buf[sizeof(inotify_event) + NAME_MAX + 1];
  ssize_t n = 0;
  while ((n = read(fd_, buf, sizeof(buf))) > 0) {
    for (char* ptr = buf; ptr < buf + n;) {
      const auto* ev = reinterpret_cast<const inotify_event*>(ptr);
      // Only honor events for the watch currently live on wd_. Capture the
      // wd this event belongs to before arm() (called below) can replace
      // wd_ with a new watch descriptor.
      if (ev->wd == wd_) {
        if (ev->mask & (IN_MOVE_SELF | IN_DELETE_SELF)) {
          arm();  // directory itself replaced: re-watch
        } else if ((ev->mask & (IN_CLOSE_WRITE | IN_MOVED_TO)) && ev->len > 0 &&
                   base_ == ev->name) {
          pending_ = true;
          deadline_ = std::chrono::steady_clock::now() + kDebounce;
        }
      }
      ptr += sizeof(inotify_event) + ev->len;
    }
  }
}

bool PatchWatcher::poll_changed(int timeout_ms) {
  if (!ok()) return false;

  // Drain whatever is already queued BEFORE evaluating the debounce
  // deadline. Checking the deadline first (as the old code did) could fire
  // on a stale deadline from an earlier write while a second write's event
  // still sits unread in the kernel buffer, causing two notifications for
  // one burst instead of one.
  drain();

  if (pending_ && std::chrono::steady_clock::now() >= deadline_) {
    pending_ = false;
    return true;
  }

  int wait_ms = timeout_ms;
  if (pending_) {
    const auto until = std::chrono::duration_cast<std::chrono::milliseconds>(
        deadline_ - std::chrono::steady_clock::now()).count();
    // NOLINTNEXTLINE(clang-diagnostic-sign-conversion)
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
