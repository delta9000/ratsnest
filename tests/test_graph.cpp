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

TEST_CASE("bareword arrays lower to strs; numeric arrays stay numeric") {
  auto r = go("q = quant { scale: [sine, abs, fold] }\nq -> out.in\n");
  REQUIRE(r.ok());
  int qi = -1;
  for (int i = 0; i < (int)r.graph->nodes.size(); ++i)
    if (r.graph->nodes[i].name == "q") qi = i;
  REQUIRE(qi >= 0);
  const auto& strs = dsp::cfg_str_arr(r.graph->nodes[qi].config, "scale");
  REQUIRE(strs.size() == 3);
  CHECK(strs[0] == "sine");
  CHECK(strs[1] == "abs");
  CHECK(strs[2] == "fold");
  CHECK(dsp::cfg_arr(r.graph->nodes[qi].config, "scale").empty());

  auto r2 = go("p = quant { scale: [0, 3, 5] }\np -> out.in\n");
  REQUIRE(r2.ok());
  int pi = -1;
  for (int i = 0; i < (int)r2.graph->nodes.size(); ++i)
    if (r2.graph->nodes[i].name == "p") pi = i;
  REQUIRE(pi >= 0);
  const auto& a = dsp::cfg_arr(r2.graph->nodes[pi].config, "scale");
  REQUIRE(a.size() == 3);
  CHECK(a[0] == 0.0);
  CHECK(a[2] == 5.0);
  CHECK(dsp::cfg_str_arr(r2.graph->nodes[pi].config, "scale").empty());
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

TEST_CASE("dotted module names resolve by longest prefix") {
  auto r = go("a.b = vca\nc = vca\nc -> a.b.cv : 2\na.b -> c\n");
  REQUIRE(r.ok());
}

TEST_CASE("'out' is reserved") {
  auto p = dsl::parse("out = vca\n");
  REQUIRE(p.ok());
  auto r = graph::compile(p.ast, 48000.0);
  REQUIRE(r.errors.size() == 1);
  CHECK(r.errors[0].msg.find("reserved") != std::string::npos);
}

TEST_CASE("note-name aliases resolve to Hz frequencies") {
  auto freq_of = [](const char* src, const char* name) {
    auto r = go(src);
    REQUIRE(r.ok());
    for (auto& n : r.graph->nodes)
      if (n.name == name) return n.input_base[0];  // vco freq is slot 0
    REQUIRE(false);
    return 0.0f;
  };
  CHECK(freq_of("o = vco { freq: A4 }\no -> out.in\n", "o") == doctest::Approx(440.0f));
  CHECK(freq_of("c = vco { freq: C4 }\nc -> out.in\n", "c") ==
        doctest::Approx(261.6256f).epsilon(1e-4));
  CHECK(freq_of("a = vco { freq: A1 }\na -> out.in\n", "a") ==
        doctest::Approx(55.0f).epsilon(1e-4));
  // enharmonic sharp/flat: Cs4 == Db4 == 277.18 Hz
  CHECK(freq_of("s = vco { freq: Cs4 }\ns -> out.in\n", "s") ==
        doctest::Approx(277.1826f).epsilon(1e-3));
  CHECK(freq_of("f = vco { freq: Db4 }\nf -> out.in\n", "f") ==
        doctest::Approx(277.1826f).epsilon(1e-3));
  // lowercase enum barewords are NOT notes and pass through unchanged
  auto re = go("w = vco { wave: saw }\nw -> out.in\n");
  REQUIRE(re.ok());
  for (auto& n : re.graph->nodes)
    if (n.name == "w") CHECK(dsp::cfg_str(n.config, "wave", "") == "saw");
}

TEST_CASE("unresolved identifier cable depth is a compile error") {
  auto p = dsl::parse("a = vca\nb = vca\na -> b.in : click\n");
  REQUIRE(p.ok());
  auto c = graph::compile(p.ast, 48000.0);
  REQUIRE_FALSE(c.ok());
  CHECK(c.errors[0].msg.find("click") != std::string::npos);
}

TEST_CASE("invalid note names report a clean error, not a crash") {
  auto p = dsl::parse("o = vco { freq: Cx }\no -> out.in\n");   // no octave digit
  CHECK(!p.ok());
  auto p2 = dsl::parse("o = vco { freq: Cs }\no -> out.in\n");  // sharp but no octave
  CHECK(!p2.ok());
  // a note also works as a plain array element (lowered to a number)
  auto r = go("o = vco { freq: A4 }\no -> out.in\n");
  REQUIRE(r.ok());
}
