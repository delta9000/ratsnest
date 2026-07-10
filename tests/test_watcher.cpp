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
  CHECK(changed_within(w, 1000));
}

TEST_CASE("rename-replace save (vim/vscode style) is detected") {
  TmpPatch t;
  PatchWatcher w(t.file.string());
  REQUIRE(w.ok());
  t.write_via_rename("a = vca { gain: 0.7 }\n");
  CHECK(changed_within(w, 1000));
}

TEST_CASE("burst of writes debounces to one notification") {
  TmpPatch t;
  PatchWatcher w(t.file.string());
  REQUIRE(w.ok());
  t.write("x = vca\n");
  std::this_thread::sleep_for(std::chrono::milliseconds(10));
  t.write_via_rename("y = vca\n");
  CHECK(changed_within(w, 1000));
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
