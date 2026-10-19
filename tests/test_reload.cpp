#include <doctest/doctest.h>
#include <memory>
#include <string>
#include "dsl/parser.h"
#include "engine/engine.h"
#include "graph/graph.h"
#include "graph/reload.h"

using namespace ms;

static std::unique_ptr<graph::Graph> g(const char* src) {
  auto p = dsl::parse(src);
  REQUIRE(p.ok());
  auto c = graph::compile(p.ast, 48000.0);
  REQUIRE(c.ok());
  return std::move(c.graph);
}

static int node_index(const graph::Graph& gr, const char* name) {
  for (size_t i = 0; i < gr.nodes.size(); ++i)
    if (gr.nodes[i].name == name) return static_cast<int>(i);
  return -1;
}

TEST_CASE("param-only edit yields a param plan with just the changed bases") {
  auto a = g("osc = vco { freq: 110hz, wave: saw }\nosc -> out.in\n");
  auto b = g("osc = vco { freq: 220hz, wave: saw }\nosc -> out.in\n");
  auto plan = graph::plan_reload(*a, *b);
  CHECK(!plan.topology);
  REQUIRE(plan.params.size() == 1);
  CHECK(plan.params[0].node == node_index(*b, "osc"));
  CHECK(plan.params[0].slot == 0);  // freq is vco input slot 0
  CHECK(plan.params[0].target == doctest::Approx(220.0f));
}

TEST_CASE("cable depth change is a topology change (cables are structure)") {
  auto a = g("osc = vco\nosc -> out.in : 0.5\n");
  auto b = g("osc = vco\nosc -> out.in : 0.9\n");
  CHECK(graph::plan_reload(*a, *b).topology);
}

TEST_CASE("identical sources yield an empty param plan") {
  auto a = g("osc = vco { freq: 110hz }\nosc -> out.in\n");
  auto b = g("osc = vco { freq: 110hz }\nosc -> out.in\n");
  auto plan = graph::plan_reload(*a, *b);
  CHECK(!plan.topology);
  CHECK(plan.params.empty());
}

TEST_CASE("config enum change is a topology change (needs re-init)") {
  auto a = g("osc = vco { wave: saw }\nosc -> out.in\n");
  auto b = g("osc = vco { wave: sine }\nosc -> out.in\n");
  CHECK(graph::plan_reload(*a, *b).topology);
}

TEST_CASE("operator waveform edits are not lost on hot reload") {
  auto a = g("osc = fm { waves: [sine, sine] }\nosc -> out.in\n");
  auto b = g("osc = fm { waves: [sine, fold] }\nosc -> out.in\n");
  CHECK(graph::plan_reload(*a, *b).topology);
}

TEST_CASE("added module: topology plan pairs surviving (name,type) modules") {
  auto a = g("osc = vco\nosc -> out.in\n");
  auto b = g("osc = vco\nlfo1 = lfo\nosc -> out.in\nlfo1 -> osc.v_oct : 0.1\n");
  auto plan = graph::plan_reload(*a, *b);
  REQUIRE(plan.topology);
  bool osc_paired = false, out_paired = false;
  for (auto [oi, ni] : b->migrate_pairs) {
    CHECK(a->nodes[static_cast<size_t>(oi)].name ==
          b->nodes[static_cast<size_t>(ni)].name);
    if (b->nodes[static_cast<size_t>(ni)].name == "osc") osc_paired = true;
    if (b->nodes[static_cast<size_t>(ni)].name == "out") out_paired = true;
  }
  CHECK(osc_paired);
  CHECK(out_paired);
  for (auto [oi, ni] : b->migrate_pairs)  // lfo1 is new: never paired
    CHECK(b->nodes[static_cast<size_t>(ni)].name != "lfo1");
}

TEST_CASE("renamed module is not paired (rename intentionally resets state)") {
  auto a = g("osc = vco\nosc -> out.in\n");
  auto b = g("osc2 = vco\nosc2 -> out.in\n");
  auto plan = graph::plan_reload(*a, *b);
  REQUIRE(plan.topology);
  for (auto [oi, ni] : b->migrate_pairs)
    CHECK(b->nodes[static_cast<size_t>(ni)].name != "osc2");
}

TEST_CASE("same name, different type is not paired") {
  auto a = g("x = vco\nx -> out.in\n");
  auto b = g("x = lfo\nx -> out.in\n");
  auto plan = graph::plan_reload(*a, *b);
  REQUIRE(plan.topology);
  for (auto [oi, ni] : b->migrate_pairs)
    CHECK(b->nodes[static_cast<size_t>(ni)].name != "x");
}

// A voice group's expanded members are ordinary named nodes (`lead.osc#0`,
// `lead.osc#1`, ...) once compiled: reload should treat each instance
// exactly like any other node, migrating per-instance state independently.
TEST_CASE("voice members migrate per-instance vco phase across a topology reload") {
  const char* src_a =
      "poly = alloc { n: 2 }\n"
      "lead = voice { notes: poly }\n"
      "lead.osc = vco\n"
      "lead.amp = vca\n"
      "poly.spread -> lead.osc.v_oct\n"  // -1 vs +1 octave: instances diverge
      "lead.osc -> lead.amp.in\n"
      "lead.amp -> out.in\n";
  // Edits a cable depth inside a member (case 1, instance-local): a topology
  // change (cables are structure) that leaves every node's (name, type,
  // config) untouched, so migrate_pairs should carry every instance.
  const char* src_b =
      "poly = alloc { n: 2 }\n"
      "lead = voice { notes: poly }\n"
      "lead.osc = vco\n"
      "lead.amp = vca\n"
      "poly.spread -> lead.osc.v_oct\n"
      "lead.osc -> lead.amp.in : 0.5\n"
      "lead.amp -> out.in\n";

  auto pa = dsl::parse(src_a);
  REQUIRE(pa.ok());
  auto ca = graph::compile(pa.ast, 48000.0);
  REQUIRE(ca.ok());
  engine::Engine a(std::move(ca.graph));

  float l[1], r[1];
  for (int i = 0; i < 100; ++i) a.process(l, r, 1);  // let the two phases diverge

  auto pb = dsl::parse(src_b);
  REQUIRE(pb.ok());
  auto cb = graph::compile(pb.ast, 48000.0);
  REQUIRE(cb.ok());
  auto plan = graph::plan_reload(a.graph(), *cb.graph);
  REQUIRE(plan.topology);

  engine::Engine b(std::move(cb.graph));
  b.migrate_from(a);

  int osc0_a = node_index(a.graph(), "lead.osc#0");
  int osc1_a = node_index(a.graph(), "lead.osc#1");
  int osc0_b = node_index(b.graph(), "lead.osc#0");
  int osc1_b = node_index(b.graph(), "lead.osc#1");
  REQUIRE((osc0_a >= 0 && osc1_a >= 0 && osc0_b >= 0 && osc1_b >= 0));

  // Each instance's vco output slot 0 is its audio out; with distinct
  // v_oct per instance, a swapped #0/#1 migrate mapping would desync one of
  // these two checks.
  for (int i = 0; i < 8; ++i) {
    a.process(l, r, 1);
    b.process(l, r, 1);
    CHECK(a.output_value(static_cast<size_t>(osc0_a), 0) ==
          doctest::Approx(b.output_value(static_cast<size_t>(osc0_b), 0)));
    CHECK(a.output_value(static_cast<size_t>(osc1_a), 0) ==
          doctest::Approx(b.output_value(static_cast<size_t>(osc1_b), 0)));
  }
}

// Changing the bound allocator's `n` changes the expansion count itself
// (spec §3): existing instances (name, type) unchanged must still migrate;
// the newly-created instance has no old-side counterpart and so is never a
// migrate target, i.e. it starts from a fresh Module (spec leaves alloc's
// own internal reload semantics across a changing n unspecified -- this
// characterizes only the *member* node identity, not alloc's `newest_`).
TEST_CASE("voice group allocator n 2->3: existing instances migrate, new instance is fresh") {
  auto a = g(
      "poly = alloc { n: 2 }\n"
      "lead = voice { notes: poly }\n"
      "lead.osc = vco\n"
      "lead.osc -> out.in\n");
  auto b = g(
      "poly = alloc { n: 3 }\n"
      "lead = voice { notes: poly }\n"
      "lead.osc = vco\n"
      "lead.osc -> out.in\n");
  auto plan = graph::plan_reload(*a, *b);
  REQUIRE(plan.topology);

  bool osc0_paired = false, osc1_paired = false;
  for (auto [oi, ni] : b->migrate_pairs) {
    const std::string& name = b->nodes[static_cast<size_t>(ni)].name;
    CHECK(name != "lead.osc#2");  // the new instance is never a migrate target
    if (name == "lead.osc#0") {
      osc0_paired = true;
      CHECK(a->nodes[static_cast<size_t>(oi)].name == "lead.osc#0");
    }
    if (name == "lead.osc#1") {
      osc1_paired = true;
      CHECK(a->nodes[static_cast<size_t>(oi)].name == "lead.osc#1");
    }
  }
  CHECK(osc0_paired);
  CHECK(osc1_paired);
  CHECK(node_index(*b, "lead.osc#2") >= 0);  // exists, just never migrated into
}
