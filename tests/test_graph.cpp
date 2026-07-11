#include <doctest/doctest.h>
#include "dsl/parser.h"
#include "graph/graph.h"

using namespace ms;

static graph::CompileResult go(const char* src) {
  auto p = dsl::parse(src);
  REQUIRE(p.ok());
  return graph::compile(p.ast, 48000.0);
}

TEST_CASE("compiles a chain in topo order with implicit out") {
  auto r = go("b = vca\na = vca\na -> b\nb -> out.in\n");
  REQUIRE(r.ok());
  REQUIRE(r.graph->out_node >= 0);
  // 'a' must be processed before 'b'
  int ia = -1, ib = -1;
  for (int i = 0; i < (int)r.graph->nodes.size(); ++i) {
    if (r.graph->nodes[i].name == "a") ia = i;
    if (r.graph->nodes[i].name == "b") ib = i;
  }
  CHECK(ia < ib);
}

TEST_CASE("declared params override input defaults") {
  auto r = go("a = vca { gain: 0.25 }\na -> out.in\n");
  REQUIRE(r.ok());
  auto& n = r.graph->nodes[0].name == "a" ? r.graph->nodes[0]
            : r.graph->nodes[r.graph->out_node == 0 ? 1 : 0];
  CHECK(n.input_base[2] == doctest::Approx(0.25f));  // gain slot
}

TEST_CASE("default ports resolve to in/out") {
  auto r = go("a = vca\nb = vca\na -> b : 0.5\n");
  REQUIRE(r.ok());
  bool found = false;
  for (auto& per_node : r.graph->in_cables)
    for (auto& c : per_node) {
      CHECK(c.src_out == 0);
      CHECK(c.dst_in == 0);
      CHECK(c.depth == doctest::Approx(0.5f));
      found = true;
    }
  CHECK(found);
}

TEST_CASE("literal source folds into base") {
  auto r = go("a = vca\n0.5 -> a.cv : 2\n");
  REQUIRE(r.ok());
  for (auto& n : r.graph->nodes)
    if (n.name == "a") CHECK(n.input_base[1] == doctest::Approx(1.0f));
  for (auto& per_node : r.graph->in_cables) CHECK(per_node.empty());
}

TEST_CASE("bare source resolves to first output, whatever its name") {
  // stepseq's first output is cv, not out; svf's is lp.
  auto r = go("s = stepseq { steps: [0.5] }\nf = svf\na = vca\ns -> a.cv\nf -> a\n");
  REQUIRE(r.ok());
  for (auto& per_node : r.graph->in_cables)
    for (auto& c : per_node) CHECK(c.src_out == 0);
}

TEST_CASE("cycles compile (feedback allowed)") {
  auto r = go("a = vca\nb = vca\na -> b\nb -> a.cv\n");
  CHECK(r.ok());
}

TEST_CASE("errors: unknown type, module, port, param, enum value") {
  auto p = dsl::parse(
      "a = flanger\n"
      "b = vca { wetness: 1 }\n"
      "c = vca { response: sideways }\n"
      "b -> ghost\n"
      "b -> c.nope\n");
  REQUIRE(p.ok());
  auto r = graph::compile(p.ast, 48000.0);
  REQUIRE(r.errors.size() == 5);
  CHECK(r.errors[0].msg.find("unknown module type 'flanger'") != std::string::npos);
  CHECK(r.errors[1].msg.find("no parameter 'wetness'") != std::string::npos);
  CHECK(r.errors[2].msg.find("lin") != std::string::npos);  // lists valid values
  CHECK(r.errors[3].msg.find("unknown module 'ghost'") != std::string::npos);
  CHECK(r.errors[4].msg.find("no input 'nope'") != std::string::npos);
}

TEST_CASE("'out' is reserved") {
  auto p = dsl::parse("out = vca\n");
  REQUIRE(p.ok());
  auto r = graph::compile(p.ast, 48000.0);
  REQUIRE(r.errors.size() == 1);
  CHECK(r.errors[0].msg.find("reserved") != std::string::npos);
}
