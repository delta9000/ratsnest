// Graphs in production are only ever built by graph::compile() from parsed
// DSL (see tests/test_reload.cpp, tests/test_engine.cpp), and the module
// registry (src/dsp/registry.cpp) is a static array — a test-only module
// type can't be registered under a patch name for compile() to find. No
// shipped module sets ModuleInfo::wants_midi yet (Task 8 adds `midi`), so
// there is no way to exercise on_midi through a real patch string today.
//
// The honest seam, matching how tests/test_module.cpp builds modules
// directly instead of through the DSL: hand-build a graph::Graph object with
// a test-local Module subclass as one of its nodes, wrap it in a real
// engine::Engine, and drive it through the real engine::RtRunner — the same
// production path src/engine/rt_runner.cpp uses (Swap command, then
// render()). This exercises the actual block-start delivery code, not a
// reimplementation of it.
//
// Task 9 moved the MIDI ring off Engine and onto RtRunner (see rt_runner.h):
// a topology hot-reload swaps in a brand-new Engine, and a ring owned by the
// outgoing Engine would silently drop events queued right at the swap. So
// these tests push through RtRunner::push_midi, not Engine::push_midi (which
// no longer exists) -- the ring must be created (RtRunner constructed)
// before events can be queued, but need not wait for an engine to be
// adopted; queued events simply wait for the first Swap to land.
#include <doctest/doctest.h>
#include <cstdint>
#include <memory>
#include <vector>
#include "dsp/module.h"
#include "dsp/modules.h"
#include "engine/engine.h"
#include "engine/rt_runner.h"
#include "graph/graph.h"

using namespace ms;

namespace {

// Records every MIDI event delivered via on_midi, in delivery order.
class MidiStub : public dsp::Module {
 public:
  struct Event {
    uint8_t data[4] = {};
    size_t len = 0;
    size_t frame = 0;  // samples processed before it arrived
  };

  void process(const float*, float*) override { ++frames; }

  void on_midi(const uint8_t* data, size_t len) override {
    Event e;
    e.len = len;
    e.frame = frames;
    for (size_t i = 0; i < len && i < 4; ++i) e.data[i] = data[i];
    received.push_back(e);
  }

  std::vector<Event> received;
  size_t frames = 0;
};

const dsp::ModuleInfo kMidiStubInfo{
    "midi_stub_test_only",
    {},
    {},
    {},
    []() -> std::unique_ptr<dsp::Module> { return std::make_unique<MidiStub>(); },
    /*wants_midi=*/true,
};

// Minimal two-node graph: the midi stub plus the real `out` module (Engine's
// process() unconditionally reads g_->out_node). No cables are needed; the
// test only asserts on_midi delivery, not audio.
std::unique_ptr<graph::Graph> make_stub_graph() {
  auto g = std::make_unique<graph::Graph>();
  g->sample_rate = 48000.0;

  graph::Node stub;
  stub.name = "stub";
  stub.info = &kMidiStubInfo;
  stub.instance = kMidiStubInfo.create();
  stub.instance->init(g->sample_rate, {});
  g->nodes.push_back(std::move(stub));

  const dsp::ModuleInfo* out_info = dsp::find_module("out");
  REQUIRE(out_info != nullptr);
  graph::Node out;
  out.name = "out";
  out.info = out_info;
  out.input_base.assign(out_info->inputs.size(), 0.0f);
  out.instance = out_info->create();
  out.instance->init(g->sample_rate, {});
  g->nodes.push_back(std::move(out));

  g->in_cables.assign(g->nodes.size(), {});
  g->out_node = 1;
  return g;
}

}  // namespace

TEST_CASE("midi events queued before a block are delivered at block start, in order") {
  auto g = make_stub_graph();
  auto* eng = new engine::Engine(std::move(g));  // ownership passes to the runner
  auto* stub = dynamic_cast<MidiStub*>(eng->graph().nodes[0].instance.get());
  REQUIRE(stub != nullptr);

  engine::RtRunner run(48000.0);
  REQUIRE(run.push_midi({{0x90, 60, 100, 0}, 3}));
  REQUIRE(run.push_midi({{0x90, 64, 100, 0}, 3}));
  REQUIRE(run.push_midi({{0x80, 60, 0, 0}, 3}));

  engine::Command swap;
  swap.kind = engine::Command::Kind::Swap;
  swap.engine = eng;
  REQUIRE(run.push(swap));

  float l[8], r[8];
  run.render(l, r, 8);  // drains commands (adopts eng), then delivers, then processes

  REQUIRE(stub->received.size() == 3);
  CHECK(stub->received[0].data[1] == 60);
  CHECK(stub->received[1].data[1] == 64);
  CHECK(stub->received[2].data[0] == 0x80);
  CHECK(stub->received[2].data[1] == 60);

  run.render(l, r, 8);  // second block: ring already drained
  CHECK(stub->received.size() == 3);
}

TEST_CASE("midi ring overflow drops the newest event, keeps queued order") {
  auto g = make_stub_graph();
  auto* eng = new engine::Engine(std::move(g));
  auto* stub = dynamic_cast<MidiStub*>(eng->graph().nodes[0].instance.get());
  REQUIRE(stub != nullptr);

  engine::RtRunner run(48000.0);

  // Capacity is 256 with usable slots Cap-1 (see rt/spsc.h); fill it past
  // that and confirm push_midi reports failure without corrupting the queue.
  int pushed = 0;
  for (int i = 0; i < 300; ++i) {
    dsp::MidiEvent e{{0x90, static_cast<uint8_t>(i), 100, 0}, 3};
    if (run.push_midi(e)) ++pushed;
  }
  CHECK(pushed == 255);  // Cap-1 usable slots

  engine::Command swap;
  swap.kind = engine::Command::Kind::Swap;
  swap.engine = eng;
  REQUIRE(run.push(swap));

  float l[4], r[4];
  run.render(l, r, 4);
  REQUIRE(stub->received.size() == 255);
  CHECK(stub->received[0].data[1] == 0);       // oldest survives
  CHECK(stub->received[254].data[1] == 254);   // the 256th+ pushes were dropped
}

TEST_CASE("timed midi events land on the frame they were stamped for") {
  auto* eng = new engine::Engine(make_stub_graph());
  auto* stub = dynamic_cast<MidiStub*>(eng->graph().nodes[0].instance.get());
  REQUIRE(stub != nullptr);
  engine::RtRunner run(48000.0);
  engine::Command swap;
  swap.kind = engine::Command::Kind::Swap;
  swap.engine = eng;
  REQUIRE(run.push(swap));

  constexpr uint64_t kT0 = 5'000'000'000;  // any monotonic time
  auto at = [](uint64_t frame) { return kT0 + frame * 1'000'000'000 / 48000 + 1; };
  float l[256], r[256];
  run.render(l, r, 256, kT0 - 256 * 1'000'000'000ull / 48000);  // adopt the engine
  stub->frames = 0;

  REQUIRE(run.push_midi({{0x90, 60, 100, 0}, 3}, kT0 - 1000));      // late: plays now
  REQUIRE(run.push_midi({{0x90, 62, 100, 0}, 3}, at(100)));
  REQUIRE(run.push_midi({{0x90, 64, 100, 0}, 3}, at(100)));          // same frame, in order
  REQUIRE(run.push_midi({{0x90, 65, 100, 0}, 3}, at(300)));          // next callback
  run.render(l, r, 256, kT0);
  REQUIRE(stub->received.size() == 3);
  CHECK(stub->received[0].frame == 0);
  CHECK(stub->received[1].frame == 100);
  CHECK(stub->received[2].frame == 100);
  CHECK(stub->received[2].data[1] == 64);
  CHECK(stub->frames == 256);

  run.render(l, r, 256, kT0 + 256 * 1'000'000'000ull / 48000);
  REQUIRE(stub->received.size() == 4);
  CHECK(stub->received[3].frame == 300);

  // An event stamped far ahead means the clocks disagree: it plays now.
  REQUIRE(run.push_midi({{0x80, 60, 0, 0}, 3}, kT0 + 60'000'000'000ull));
  run.render(l, r, 256, kT0 + 512 * 1'000'000'000ull / 48000);
  REQUIRE(stub->received.size() == 5);
  CHECK(stub->received[4].frame == 512);
}
