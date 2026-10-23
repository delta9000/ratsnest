#include <doctest/doctest.h>
#include <chrono>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <thread>
#include <unistd.h>
#include "io/watcher.h"

namespace fs = std::filesystem;
using ms::io::PatchWatcher;

struct TmpPatch {
  fs::path dir, file;
  TmpPatch() {
    dir = fs::temp_directory_path() / ("mswatch-" + std::to_string(::getpid()));
    fs::create_directories(dir);
    file = dir / "t.patch";
    write("a = vca\n");
  }
  ~TmpPatch() { std::error_code ec; fs::remove_all(dir, ec); }
  void write(const char* text) {
    std::ofstream f(file);
    f << text;
  }
  void write_via_rename(const char* text) {  // how vim/vscode save
    fs::path tmp = dir / "t.patch.tmp";
    { std::ofstream f(tmp); f << text; }
    fs::rename(tmp, file);
  }
};

static bool changed_within(PatchWatcher& w, int total_ms) {
  auto end = std::chrono::steady_clock::now() + std::chrono::milliseconds(total_ms);
  while (std::chrono::steady_clock::now() < end)
    if (w.poll_changed(20)) return true;
  return false;
}

TEST_CASE("in-place write is detected after debounce") {
  TmpPatch t;
  PatchWatcher w(t.file.string());
  REQUIRE(w.ok());
  CHECK(!w.poll_changed(10));  // quiet
  t.write("a = vca { gain: 0.5 }\n");
  CHECK(changed_within(w, 2000));
}

TEST_CASE("rename-replace save (vim/vscode style) is detected") {
  TmpPatch t;
  PatchWatcher w(t.file.string());
  REQUIRE(w.ok());
  t.write_via_rename("a = vca { gain: 0.7 }\n");
  CHECK(changed_within(w, 2000));
}

TEST_CASE("burst of writes debounces to one notification") {
  TmpPatch t;
  PatchWatcher w(t.file.string());
  REQUIRE(w.ok());
  t.write("x = vca\n");
  std::this_thread::sleep_for(std::chrono::milliseconds(10));
  t.write_via_rename("y = vca\n");
  CHECK(changed_within(w, 2000));
  CHECK(!changed_within(w, 150));  // no second event for the same burst
}

TEST_CASE("writes to sibling files are ignored") {
  TmpPatch t;
  PatchWatcher w(t.file.string());
  REQUIRE(w.ok());
  { std::ofstream f(t.dir / "other.patch"); f << "b = vca\n"; }
  CHECK(!changed_within(w, 200));
}

TEST_CASE("missing directory reports not-ok") {
  PatchWatcher w("/nonexistent-dir-xyz/p.patch");
  CHECK(!w.ok());
}

TEST_CASE("two writes separated by an unpolled gap still coalesce to one notification") {
  // Regression for: poll_changed checking the quiescence deadline BEFORE
  // draining the inotify fd. Write A and consume its event with a single
  // poll_changed call (arms the debounce timer, doesn't fire yet). Then let
  // a full debounce window pass WITHOUT calling poll_changed again, so the
  // deadline goes stale. Write B while nobody is polling (its event sits
  // unread in the kernel buffer). Now resume polling.
  //
  // Buggy ordering: entry check sees the stale deadline and returns true
  // immediately, WITHOUT draining the fd, so B's event is left for the next
  // call, which re-arms and fires again ~50ms later: two notifications for
  // one burst. Correct ordering: drain first (finds B, extends the
  // deadline), so the stale-deadline path is never taken and only one
  // notification fires.
  TmpPatch t;
  PatchWatcher w(t.file.string());
  REQUIRE(w.ok());

  t.write("x = vca\n");
  // Consumes A's event and arms pending_/deadline_ without waiting: inotify
  // queues the event synchronously at close(), so a zero timeout sees it. A
  // non-zero timeout here raced the 50 ms debounce under parallel load (poll
  // oversleeping its wait let A fire early and the test fail spuriously).
  CHECK(!w.poll_changed(0));

  std::this_thread::sleep_for(std::chrono::milliseconds(80));  // > 50ms debounce, unpolled
  t.write_via_rename("y = vca\n");  // B's event now sits unread in the kernel

  // Wait with a generous deadline for the one notification, then watch a
  // further quiet window (several debounce periods) for a spurious second.
  int fires = 0;
  auto end = std::chrono::steady_clock::now() + std::chrono::milliseconds(2000);
  while (fires == 0 && std::chrono::steady_clock::now() < end)
    if (w.poll_changed(20)) ++fires;
  REQUIRE(fires == 1);
  end = std::chrono::steady_clock::now() + std::chrono::milliseconds(400);
  while (std::chrono::steady_clock::now() < end)
    if (w.poll_changed(20)) ++fires;
  CHECK(fires == 1);
}

TEST_CASE("watched directory being moved away does not crash or spuriously fire") {
  TmpPatch t;
  PatchWatcher w(t.file.string());
  REQUIRE(w.ok());

  fs::path moved = t.dir.parent_path() / (t.dir.filename().string() + "-moved");
  fs::rename(t.dir, moved);

  CHECK(!changed_within(w, 200));
  CHECK(!w.ok());  // re-arm on the now-stale path fails

  // avoid double-removal: TmpPatch's destructor removes the original dir_,
  // which no longer exists; restore it so cleanup is a no-op either way.
  std::error_code ec;
  fs::rename(moved, t.dir, ec);
}

TEST_CASE("watcher fires for any of several files") {
  namespace fs = std::filesystem;
  // Per-process name: concurrent test runs (other build trees) share /tmp.
  const fs::path dir =
      fs::temp_directory_path() / ("ms_watch_multi-" + std::to_string(::getpid()));
  fs::create_directories(dir / "lib");
  const auto touch = [](const fs::path& p) {
    std::ofstream f(p);
    f << "# x\n";
  };
  touch(dir / "song.patch");
  touch(dir / "lib" / "kick.sub");
  ms::io::PatchWatcher w(std::vector<std::string>{
      (dir / "song.patch").string(), (dir / "lib" / "kick.sub").string()});
  REQUIRE(w.ok());
  CHECK_FALSE(w.poll_changed(0));
  touch(dir / "lib" / "kick.sub");
  CHECK(changed_within(w, 2000));
  fs::remove_all(dir);
}
