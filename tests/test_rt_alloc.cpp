// Design rule 3, enforced: nothing reachable from the audio thread allocates.
// This TU replaces the global operator new/delete for the whole test binary
// with malloc/free plus a per-thread counter. Counting is off unless a test
// arms it, so every other test sees plain malloc behaviour.
#include <doctest/doctest.h>
#include <algorithm>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <new>
#include <string>
#include <vector>
#include "engine/controller.h"
#include "engine/rt_runner.h"

namespace {
thread_local bool t_armed = false;
thread_local long t_allocs = 0;
thread_local long t_frees = 0;

void* counted_alloc(std::size_t n, std::size_t align) {
  if (t_armed) ++t_allocs;
  if (n == 0) n = 1;
  void* p = nullptr;
  if (align <= alignof(std::max_align_t)) {
    p = std::malloc(n);
  } else if (posix_memalign(&p, align, n) != 0) {
    p = nullptr;
  }
  return p;
}
void counted_free(void* p) {
  if (p != nullptr && t_armed) ++t_frees;
  std::free(p);
}
void* or_throw(void* p) {
  if (p == nullptr) throw std::bad_alloc();
  return p;
}
constexpr std::size_t kDefaultAlign = alignof(std::max_align_t);
}  // namespace

void* operator new(std::size_t n) { return or_throw(counted_alloc(n, kDefaultAlign)); }
void* operator new[](std::size_t n) { return or_throw(counted_alloc(n, kDefaultAlign)); }
void* operator new(std::size_t n, const std::nothrow_t&) noexcept {
  return counted_alloc(n, kDefaultAlign);
}
void* operator new[](std::size_t n, const std::nothrow_t&) noexcept {
  return counted_alloc(n, kDefaultAlign);
}
void* operator new(std::size_t n, std::align_val_t a) {
  return or_throw(counted_alloc(n, static_cast<std::size_t>(a)));
}
void* operator new[](std::size_t n, std::align_val_t a) {
  return or_throw(counted_alloc(n, static_cast<std::size_t>(a)));
}
void* operator new(std::size_t n, std::align_val_t a, const std::nothrow_t&) noexcept {
  return counted_alloc(n, static_cast<std::size_t>(a));
}
void* operator new[](std::size_t n, std::align_val_t a, const std::nothrow_t&) noexcept {
  return counted_alloc(n, static_cast<std::size_t>(a));
}
void operator delete(void* p) noexcept { counted_free(p); }
void operator delete[](void* p) noexcept { counted_free(p); }
void operator delete(void* p, std::size_t) noexcept { counted_free(p); }
void operator delete[](void* p, std::size_t) noexcept { counted_free(p); }
void operator delete(void* p, const std::nothrow_t&) noexcept { counted_free(p); }
void operator delete[](void* p, const std::nothrow_t&) noexcept { counted_free(p); }
void operator delete(void* p, std::align_val_t) noexcept { counted_free(p); }
void operator delete[](void* p, std::align_val_t) noexcept { counted_free(p); }
void operator delete(void* p, std::size_t, std::align_val_t) noexcept { counted_free(p); }
void operator delete[](void* p, std::size_t, std::align_val_t) noexcept { counted_free(p); }
void operator delete(void* p, std::align_val_t, const std::nothrow_t&) noexcept {
  counted_free(p);
}
void operator delete[](void* p, std::align_val_t, const std::nothrow_t&) noexcept {
  counted_free(p);
}

namespace {
using ms::engine::Command;
using ms::engine::Controller;
using ms::engine::RtRunner;

const auto kRoot = std::filesystem::path(__FILE__).parent_path().parent_path();
constexpr double kRate = 48000.0;
constexpr size_t kBlock = 256;

std::vector<int>* volatile sink = nullptr;

struct Armed {  // RAII: count this thread's heap traffic while alive
  Armed() { t_allocs = t_frees = 0; t_armed = true; }
  ~Armed() { t_armed = false; }
};

std::string read_file(const std::filesystem::path& p) {
  std::ifstream f(p);
  return std::string(std::istreambuf_iterator<char>(f), std::istreambuf_iterator<char>());
}

// Feeds the RT side everything a live session can: MIDI, captured audio,
// a watch, telemetry. All of it is drained inside render().
void feed_rt_inputs(RtRunner& run, int block) {
  const uint8_t status = block % 2 == 0 ? 0x90 : 0x80;
  run.push_midi(ms::dsp::MidiEvent{{status, static_cast<uint8_t>(48 + block % 24), 100, 0}, 3});
  run.push_midi(ms::dsp::MidiEvent{{0xb0, 1, static_cast<uint8_t>(block % 128), 0}, 3});
  ms::engine::AudioChunk chunk{};
  chunk.len = 64;
  for (uint32_t i = 0; i < chunk.len; ++i) chunk.s[i] = (i % 2 == 0) ? 0.25f : -0.25f;
  run.push_audio(chunk);
}

void render_blocks(RtRunner& run, int blocks, std::vector<float>& l, std::vector<float>& r) {
  for (int b = 0; b < blocks; ++b) {
    feed_rt_inputs(run, b);
    run.render(l.data(), r.data(), kBlock);
  }
}
}  // namespace

TEST_CASE("the counting allocator sees heap traffic only while armed") {
  {
    Armed a;
    sink = new std::vector<int>(16);  // volatile: the pair cannot be elided
    delete sink;
  }
  CHECK(t_allocs == 2);  // checked disarmed: doctest may allocate itself
  CHECK(t_frees == 2);
  sink = new std::vector<int>(1);
  delete sink;
  CHECK(t_allocs == 2);  // unchanged once disarmed
}

// Every patch in the repo, through the same Controller/RtRunner pair `play`
// uses: after a warm-up block, rendering (with MIDI, captured audio, a watch
// and telemetry flowing), adopting a hot-reloaded topology (migration runs on
// the RT side), and crossfading out the old engine must not touch the heap.
TEST_CASE("audio thread never allocates after warm-up, for every patch") {
  std::vector<std::filesystem::path> patches;
  for (const auto& e : std::filesystem::recursive_directory_iterator(kRoot / "patches"))
    if (e.is_regular_file() && e.path().extension() == ".patch") patches.push_back(e.path());
  std::sort(patches.begin(), patches.end());
  REQUIRE(patches.size() > 10);

  std::vector<float> l(kBlock), r(kBlock);
  for (const auto& path : patches) {
    CAPTURE(path.lexically_relative(kRoot).string());
    const std::string src = read_file(path);
    RtRunner run(kRate);
    Controller ctl(run, kRate);
    auto first = ctl.load(src, path.string());
    if (!first.ok) continue;  // compile errors are check-all's job, not ours

    Command observe;
    observe.kind = Command::Kind::Observe;
    observe.value = 1.0f;
    REQUIRE(run.push(observe));
    run.render(l.data(), r.data(), kBlock);  // warm-up: adopt the first engine
    ms::engine::Observation obs;
    {
      Armed a;
      render_blocks(run, 12, l, r);
    }
    CHECK(t_allocs == 0);
    CHECK(t_frees == 0);
    while (run.try_observe(obs)) {}

    // Topology reload: an extra node forces a Swap with state migration.
    auto second = ctl.load(src + "\nrtallocprobe = noise\n", path.string());
    REQUIRE(second.ok);
    REQUIRE(second.topology);
    CHECK(ctl.set_watch("rtallocprobe"));
    {
      Armed a;
      // 50 ms fade at 48 kHz spans ~10 blocks of 256: cover all of it.
      render_blocks(run, 16, l, r);
    }
    CHECK(t_allocs == 0);
    CHECK(t_frees == 0);
    ctl.drain_retired();
    CHECK(ctl.engines_in_flight() == 1);
  }
}
