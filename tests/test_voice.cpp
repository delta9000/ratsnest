#include <string>
#include <doctest/doctest.h>
#include "dsl/parser.h"
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

TEST_CASE("patches without voice decls are passed through unchanged") {
  const char* src = "a = vca\nb = vco\nb -> a.in\na -> out.in\n";
  auto ast = go(src);
  CHECK(ast.modules.size() == 2);
  CHECK(ast.cables.size() == 2);
  CHECK(has_cable(ast, "b", "a.in"));
  CHECK(has_cable(ast, "a", "out.in"));
}
