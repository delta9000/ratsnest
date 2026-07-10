#include <doctest/doctest.h>
#include <memory>
#include "dsl/parser.h"
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
