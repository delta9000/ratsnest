#include <doctest/doctest.h>
#include <memory>
#include "dsl/parser.h"
#include "engine/engine.h"
#include "graph/graph.h"

using namespace ms;

static std::unique_ptr<engine::Engine> make_engine(const char* src) {
  auto p = dsl::parse(src);
  REQUIRE(p.ok());
  auto c = graph::compile(p.ast, 48000.0);
  REQUIRE(c.ok());
  return std::make_unique<engine::Engine>(std::move(c.graph));
}

static int node_index(const engine::Engine& e, const char* name) {
  const auto& nodes = e.graph().nodes;
  for (size_t i = 0; i < nodes.size(); ++i)
    if (nodes[i].name == name) return static_cast<int>(i);
  return -1;
}

// DC test rig: literal 1.0 into a vca; out follows the vca gain exactly.
static constexpr const char* kRig = "a = vca { gain: 0.0 }\n1 -> a.in\na -> out.in\n";

TEST_CASE("set_param glides linearly and lands exactly on target") {
  auto e = make_engine(kRig);
  const size_t a = static_cast<size_t>(node_index(*e, "a"));
  e->set_param(a, 2, 0.5f, 100);  // gain is input slot 2
  float l[128], r[128];
  e->process(l, r, 128);
  CHECK(l[0] == doctest::Approx(0.005f));   // first sample: one step of 0.5/100
  CHECK(l[49] == doctest::Approx(0.25f));   // halfway
  CHECK(l[99] == doctest::Approx(0.5f));    // exact arrival at sample 100
  CHECK(l[127] == doctest::Approx(0.5f));   // stays put
  for (int i = 1; i < 100; ++i) CHECK(l[i] > l[i - 1]);  // monotonic, no zipper
}

TEST_CASE("glide_samples=0 jumps immediately") {
  auto e = make_engine(kRig);
  const size_t a = static_cast<size_t>(node_index(*e, "a"));
  e->set_param(a, 2, 0.7f, 0);
  float l[4], r[4];
  e->process(l, r, 4);
  CHECK(l[0] == doctest::Approx(0.7f));
}

TEST_CASE("a new glide on the same param replaces the old one") {
  auto e = make_engine(kRig);
  const size_t a = static_cast<size_t>(node_index(*e, "a"));
  e->set_param(a, 2, 1.0f, 100);
  float l[50], r[50];
  e->process(l, r, 50);                 // halfway to 1.0 -> gain ~0.5
  e->set_param(a, 2, 0.0f, 50);         // retarget to 0 over 50 samples
  float l2[64], r2[64];
  e->process(l2, r2, 64);
  CHECK(l2[49] == doctest::Approx(0.0f));
  CHECK(l2[63] == doctest::Approx(0.0f));
  for (int i = 1; i < 50; ++i) CHECK(l2[i] < l2[i - 1]);  // ramping down
}

TEST_CASE("out-of-range set_param and output_value are safe") {
  auto e = make_engine(kRig);
  e->set_param(999, 0, 1.0f, 10);       // must not crash or write
  CHECK(e->output_value(999, 0) == 0.0f);
  float l[8], r[8];
  e->process(l, r, 8);
  CHECK(l[7] == doctest::Approx(0.0f)); // gain still 0
}

TEST_CASE("output_value reads a node's last output sample") {
  auto e = make_engine(kRig);
  const size_t a = static_cast<size_t>(node_index(*e, "a"));
  e->set_param(a, 2, 1.0f, 0);
  float l[4], r[4];
  e->process(l, r, 4);
  CHECK(e->output_value(a, 0) == doctest::Approx(1.0f));  // vca out slot 0
}
