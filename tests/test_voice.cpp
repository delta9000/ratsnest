#include <string>
#include <doctest/doctest.h>
#include "dsl/parser.h"
#include "dsp/module.h"
#include "graph/graph.h"
#include "graph/voices.h"

using namespace ms;

// Parse + expand, asserting a clean expansion (used for the success cases).
static dsl::PatchAst go(const char* src) {
  auto p = dsl::parse(src);
  REQUIRE(p.ok());
  Diags d;
  graph::expand_voices(p.ast, d);
  REQUIRE(d.empty());
  return std::move(p.ast);
}

// Parse + expand, returning only the diagnostics (used for the error cases).
static Diags diags(const char* src) {
  auto p = dsl::parse(src);
  REQUIRE(p.ok());
  Diags d;
  graph::expand_voices(p.ast, d);
  return d;
}

static std::string ep(const dsl::Endpoint& e) {
  return e.is_literal ? "#lit" : e.module;
}
static bool has_cable(const dsl::PatchAst& a, const char* s, const char* d) {
  for (const auto& c : a.cables)
    if (ep(c.src) == s && ep(c.dst) == d) return true;
  return false;
}
static bool has_module(const dsl::PatchAst& a, const char* name) {
  for (const auto& m : a.modules)
    if (m.name == name) return true;
  return false;
}

const char* kPatch =
    "poly = alloc { n: 2 }\n"
    "lead = voice { notes: poly }\n"
    "lead.osc = vco\n"
    "lead.amp = vca\n"
    "poly.voct -> lead.osc.v_oct\n"  // case 2: bare bank
    "lfo1 = lfo\n"
    "lfo1 -> lead.osc.fm\n"          // case 3: broadcast
    "lead.osc -> lead.amp.in\n"      // case 1: instance-local
    "lead.amp -> out.in\n";         // case 4: sum

TEST_CASE("members are stamped n times with #i names") {
  auto ast = go(kPatch);
  // decls: poly, lfo1, lead.osc#0, lead.osc#1, lead.amp#0, lead.amp#1
  CHECK(ast.modules.size() == 6);
  CHECK(has_module(ast, "poly"));
  CHECK(has_module(ast, "lfo1"));
  CHECK(has_module(ast, "lead.osc#0"));
  CHECK(has_module(ast, "lead.osc#1"));
  CHECK(has_module(ast, "lead.amp#0"));
  CHECK(has_module(ast, "lead.amp#1"));
  // The voice decl itself is gone.
  CHECK_FALSE(has_module(ast, "lead"));
  // Stamped members carry their type across.
  for (const auto& m : ast.modules)
    if (m.name.rfind("lead.osc#", 0) == 0) CHECK(m.type == "vco");
}

TEST_CASE("four cable cases expand correctly") {
  auto ast = go(kPatch);
  CHECK(ast.cables.size() == 2 + 2 + 2 + 2);

  // case 2 bare bank: 1-based port index binds to 0-based instance.
  CHECK(has_cable(ast, "poly.voct1", "lead.osc#0.v_oct"));
  CHECK(has_cable(ast, "poly.voct2", "lead.osc#1.v_oct"));
  // No off-by-one: instance 0 must NOT read voct2, instance 1 must NOT read voct1.
  CHECK_FALSE(has_cable(ast, "poly.voct2", "lead.osc#0.v_oct"));
  CHECK_FALSE(has_cable(ast, "poly.voct1", "lead.osc#1.v_oct"));
  CHECK_FALSE(has_cable(ast, "poly.voct0", "lead.osc#0.v_oct"));

  // case 3 broadcast: one source feeds every instance.
  CHECK(has_cable(ast, "lfo1", "lead.osc#0.fm"));
  CHECK(has_cable(ast, "lfo1", "lead.osc#1.fm"));

  // case 1 instance-local: no cross terms.
  CHECK(has_cable(ast, "lead.osc#0", "lead.amp#0.in"));
  CHECK(has_cable(ast, "lead.osc#1", "lead.amp#1.in"));
  CHECK_FALSE(has_cable(ast, "lead.osc#0", "lead.amp#1.in"));
  CHECK_FALSE(has_cable(ast, "lead.osc#1", "lead.amp#0.in"));

  // case 4 sum: every instance into the same outside port.
  CHECK(has_cable(ast, "lead.amp#0", "out.in"));
  CHECK(has_cable(ast, "lead.amp#1", "out.in"));
}

TEST_CASE("kPatch runs the full compiler: node count, in_cables shapes, "
          "and per-instance nodes for a no-output member") {
  // Extend kPatch with a `scope` member (input-only, no outputs) wired
  // instance-locally: exercises full compile (topo sort, cable resolution,
  // instantiation) for a member type with an empty output list.
  std::string src = std::string(kPatch) + "lead.scope = scope\nlead.osc -> lead.scope.in\n";
  auto p = dsl::parse(src);
  REQUIRE(p.ok());
  auto c = graph::compile(p.ast, 48000.0);
  REQUIRE(c.ok());
  const graph::Graph& g = *c.graph;

  auto idx = [&](const char* name) -> int {
    for (size_t i = 0; i < g.nodes.size(); ++i)
      if (g.nodes[i].name == name) return static_cast<int>(i);
    return -1;
  };

  // poly, lfo1, lead.osc#0/#1, lead.amp#0/#1, lead.scope#0/#1, out.
  CHECK(g.nodes.size() == 9);
  CHECK(g.in_cables.size() == g.nodes.size());
  for (const char* name : {"poly", "lfo1", "lead.osc#0", "lead.osc#1", "lead.amp#0",
                            "lead.amp#1", "lead.scope#0", "lead.scope#1", "out"})
    CHECK(idx(name) >= 0);

  int osc0 = idx("lead.osc#0"), osc1 = idx("lead.osc#1");
  int amp0 = idx("lead.amp#0"), amp1 = idx("lead.amp#1");
  int scope0 = idx("lead.scope#0"), scope1 = idx("lead.scope#1");
  REQUIRE((osc0 >= 0 && osc1 >= 0 && amp0 >= 0 && amp1 >= 0 && scope0 >= 0 && scope1 >= 0));

  auto fed_by = [&](int dst, int src_node) {
    for (const auto& cab : g.in_cables[static_cast<size_t>(dst)])
      if (cab.src_mod == src_node) return true;
    return false;
  };
  // Case 1 instance-local cables survive compile with no cross terms, for
  // both members wired to lead.osc (amp and the new no-output scope).
  CHECK(fed_by(amp0, osc0));
  CHECK(fed_by(amp1, osc1));
  CHECK_FALSE(fed_by(amp1, osc0));
  CHECK_FALSE(fed_by(amp0, osc1));
  CHECK(fed_by(scope0, osc0));
  CHECK(fed_by(scope1, osc1));
  CHECK_FALSE(fed_by(scope1, osc0));
  CHECK_FALSE(fed_by(scope0, osc1));
}

TEST_CASE("cross-group member links expand to n1 * n2 cables") {
  const char* src =
      "pa = alloc { n: 2 }\n"
      "pb = alloc { n: 3 }\n"
      "a = voice { notes: pa }\n"
      "a.m = vca\n"
      "b = voice { notes: pb }\n"
      "b.m = vca\n"
      "a.m -> b.m.in\n";
  auto ast = go(src);
  // Every a instance links to every b instance: 2 * 3.
  int links = 0;
  for (const auto& c : ast.cables)
    if (ep(c.src).rfind("a.m#", 0) == 0 && ep(c.dst).rfind("b.m#", 0) == 0) ++links;
  CHECK(links == 6);
  CHECK(has_cable(ast, "a.m#0", "b.m#0.in"));
  CHECK(has_cable(ast, "a.m#1", "b.m#2.in"));
}

TEST_CASE("voice count and banks are inherited from the bound module") {
  const char* src =
      "poly = alloc { n: 4 }\n"
      "v = voice { notes: poly }\n"
      "v.o = vco\n"
      "poly.spread -> v.o.v_oct : 0.01\n";  // spread is a bank family too
  auto ast = go(src);
  CHECK(has_module(ast, "v.o#0"));
  CHECK(has_module(ast, "v.o#3"));
  CHECK_FALSE(has_module(ast, "v.o#4"));
  CHECK(has_cable(ast, "poly.spread1", "v.o#0.v_oct"));
  CHECK(has_cable(ast, "poly.spread4", "v.o#3.v_oct"));
}

// The group's count with `n`/`voices` unset is the bound module's declared
// config default (the same constant the module's init reads), for every
// bank-producing module, so a new one cannot silently stamp the wrong count.
// A bank module with no count config (the tracker's per-track gates) is one.
TEST_CASE("an unset voice count is the bound module's declared default") {
  int banks = 0;
  for (const dsp::ModuleInfo* info : dsp::all_modules()) {
    if (info->output_index("gate1") < 0) continue;
    CAPTURE(info->type);
    ++banks;
    const dsp::ConfigInfo* c = info->config("n");
    if (!c) c = info->config("voices");
    const int want = c ? static_cast<int>(c->def) : 1;
    CHECK(want >= 1);
    const std::string src = std::string("b = ") + info->type + " {}\nv = voice { notes: b }\nv.o = vco\n";
    auto ast = go(src.c_str());
    int stamped = 0;
    while (has_module(ast, ("v.o#" + std::to_string(stamped)).c_str())) ++stamped;
    CHECK(stamped == want);
  }
  CHECK(banks >= 3);  // alloc, chord, midi, ...
}

// A dotted decl whose prefix is not a voice group is an ordinary module
// (spec §1); Task 1 established general dotted names, so this must not error.
TEST_CASE("plain dotted decl with a non-group prefix passes through") {
  auto ast = go("a.b = vca\nc = vca\nc -> a.b.cv\n");
  CHECK(ast.modules.size() == 2);
  CHECK(has_module(ast, "a.b"));
  CHECK(has_cable(ast, "c", "a.b.cv"));
}

TEST_CASE("errors: n mismatch, nested voice, no members, notes target lacks banks") {
  SUBCASE("explicit n: mismatches the bound module") {
    auto d = diags(
        "poly = alloc { n: 2 }\n"
        "lead = voice { notes: poly, n: 3 }\n"
        "lead.osc = vco\n");
    REQUIRE(d.size() == 1);
    CHECK(d[0].line == 2);
  }
  SUBCASE("member may not itself be a voice (nested)") {
    auto d = diags(
        "poly = alloc { n: 2 }\n"
        "lead = voice { notes: poly }\n"
        "lead.sub = voice { notes: poly }\n");
    REQUIRE(d.size() == 1);
    CHECK(d[0].line == 3);
  }
  SUBCASE("voice group with zero members") {
    auto d = diags(
        "poly = alloc { n: 2 }\n"
        "lead = voice { notes: poly }\n");
    REQUIRE(d.size() == 1);
    CHECK(d[0].line == 2);
  }
  SUBCASE("notes target lacks banks") {
    auto d = diags(
        "osc = vco\n"
        "lead = voice { notes: osc }\n"
        "lead.a = vca\n");
    REQUIRE(d.size() == 1);
    CHECK(d[0].line == 2);
  }
}

// Bank stems must require the *full* 8-wide family (<stem>1..<stem>8), not
// just a <stem>1/<stem>2 pair -- otherwise midi's 4-wide cc1..cc4 falsely
// qualifies as a bank, and bare `mid.cc` silently fans cc1..ccN across voice
// instances (wrong wiring at voices<=4) instead of failing as the unknown
// output it actually is.
TEST_CASE("bare mid.cc does not resolve as a bank; bare mid.voct still does") {
  const char* src =
      "mid = midi { voices: 4 }\n"
      "keys = voice { notes: mid }\n"
      "keys.osc = vco\n"
      "mid.voct -> keys.osc.v_oct\n"
      "mid.cc -> keys.osc.fm\n";
  auto p = dsl::parse(src);
  REQUIRE(p.ok());

  // voct is 8-wide on midi (voct1..voct8): still resolves per-instance.
  Diags d;
  graph::expand_voices(p.ast, d);
  REQUIRE(d.empty());
  CHECK(has_cable(p.ast, "mid.voct1", "keys.osc#0.v_oct"));
  CHECK(has_cable(p.ast, "mid.voct4", "keys.osc#3.v_oct"));
  CHECK_FALSE(has_cable(p.ast, "mid.voct5", "keys.osc#0.v_oct"));

  // cc is only 4-wide on midi (cc1..cc4) and "cc" itself is not a valid
  // output name at all, so bare `mid.cc` must NOT resolve as a bank; it
  // must fail full compile as an unknown output, not silently broadcast
  // or fan out across instances.
  auto c = graph::compile(p.ast, 48000.0);
  CHECK_FALSE(c.ok());
  bool found_cc_error = false;
  for (const auto& e : c.errors)
    if (e.msg.find("no output 'cc'") != std::string::npos) found_cc_error = true;
  CHECK(found_cc_error);
}

TEST_CASE("patches without voice decls are passed through unchanged") {
  const char* src = "a = vca\nb = vco\nb -> a.in\na -> out.in\n";
  auto ast = go(src);
  CHECK(ast.modules.size() == 2);
  CHECK(ast.cables.size() == 2);
  CHECK(has_cable(ast, "b", "a.in"));
  CHECK(has_cable(ast, "a", "out.in"));
}
