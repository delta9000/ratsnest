#include <map>
#include <string>
#include <vector>
#include <doctest/doctest.h>
#include "dsl/parser.h"
#include "graph/subs.h"

using namespace ms;

namespace {

graph::FileReader mem(std::map<std::string, std::string> files) {
  return [files = std::move(files)](const std::string& p, std::string& out) {
    auto it = files.find(p);
    if (it == files.end()) return false;
    out = it->second;
    return true;
  };
}

struct Expanded {
  dsl::PatchAst ast;
  Diags errors;
  std::vector<std::string> files;
};

Expanded expand(const char* src, std::map<std::string, std::string> files = {},
                const std::string& dir = "patches") {
  auto p = dsl::parse(src);
  REQUIRE(p.ok());
  Expanded e;
  e.ast = std::move(p.ast);
  auto r = graph::expand_subs(e.ast, dir, mem(std::move(files)), e.errors);
  e.files = std::move(r.files);
  return e;
}

const dsl::ModuleDecl* find_mod(const dsl::PatchAst& a, const std::string& name) {
  for (const auto& m : a.modules)
    if (m.name == name) return &m;
  return nullptr;
}

const dsl::ParamDecl* find_cfg(const dsl::ModuleDecl& m, const std::string& name) {
  for (const auto& p : m.params)
    if (p.name == name) return &p;
  return nullptr;
}

bool has_cable(const dsl::PatchAst& a, const std::string& s, const std::string& d) {
  for (const auto& c : a.cables)
    if (!c.src.is_literal && c.src.module == s && c.dst.module == d) return true;
  return false;
}

const dsl::CableDecl* find_cable_to(const dsl::PatchAst& a, const std::string& d) {
  for (const auto& c : a.cables)
    if (c.dst.module == d) return &c;
  return nullptr;
}

}  // namespace

static const char* kToneSub = R"(
tune = param { default: 220 }
lvl  = param { default: 0.5 }
osc  = vco { wave: sine, freq: tune }
amp  = vca { gain: 0 }
osc -> amp.in : lvl
tune -> amp.cv
)";

TEST_CASE("sub: stamps namespaced modules, substitutes params everywhere") {
  auto e = expand("t = sub { file: \"lib/tone.sub\", tune: 440 }\n",
                  {{"patches/lib/tone.sub", kToneSub}});
  REQUIRE(e.errors.empty());
  const auto* osc = find_mod(e.ast, "t.osc");
  REQUIRE(osc);
  CHECK(osc->type == "vco");
  const auto* freq = find_cfg(*osc, "freq");
  REQUIRE(freq);
  CHECK(freq->value.kind == dsl::Value::Kind::Num);
  CHECK(freq->value.num == doctest::Approx(440.0));
  const auto* into_amp = find_cable_to(e.ast, "t.amp.in");
  REQUIRE(into_amp);
  CHECK(into_amp->depth == doctest::Approx(0.5));
  CHECK(into_amp->depth_ident.empty());
  const auto* cv = find_cable_to(e.ast, "t.amp.cv");
  REQUIRE(cv);
  CHECK(cv->src.is_literal);
  CHECK(cv->src.literal == doctest::Approx(440.0));
  CHECK_FALSE(find_mod(e.ast, "t"));
  CHECK_FALSE(find_mod(e.ast, "t.tune"));
  CHECK(e.files == std::vector<std::string>{"patches/lib/tone.sub"});
}

TEST_CASE("sub: two instances of one file, different params, one parse") {
  int reads = 0;
  auto p = dsl::parse(
      "a = sub { file: \"lib/tone.sub\", tune: 100 }\n"
      "b = sub { file: \"lib/tone.sub\", tune: 200 }\n");
  REQUIRE(p.ok());
  Diags d;
  auto counting = [&](const std::string& path, std::string& out) {
    ++reads;
    if (path != "patches/lib/tone.sub") return false;
    out = kToneSub;
    return true;
  };
  auto r = graph::expand_subs(p.ast, "patches", counting, d);
  REQUIRE(d.empty());
  CHECK(reads == 1);
  CHECK(find_cfg(*find_mod(p.ast, "a.osc"), "freq")->value.num == doctest::Approx(100.0));
  CHECK(find_cfg(*find_mod(p.ast, "b.osc"), "freq")->value.num == doctest::Approx(200.0));
  CHECK(r.files.size() == 1);
}

TEST_CASE("sub errors leave the AST unmodified and carry the file") {
  SUBCASE("missing file") {
    auto e = expand("t = sub { file: \"lib/nope.sub\" }\n");
    REQUIRE_FALSE(e.errors.empty());
    CHECK(e.errors[0].msg.find("nope.sub") != std::string::npos);
    CHECK(find_mod(e.ast, "t"));
  }
  SUBCASE("unknown override key") {
    auto e = expand("t = sub { file: \"lib/tone.sub\", wat: 1 }\n",
                    {{"patches/lib/tone.sub", kToneSub}});
    REQUIRE_FALSE(e.errors.empty());
    CHECK(e.errors[0].msg.find("wat") != std::string::npos);
  }
  SUBCASE("both file: and def:") {
    auto e = expand("t = sub { file: \"x\", def: y }\n");
    REQUIRE_FALSE(e.errors.empty());
  }
  SUBCASE("parse error inside the sub file is attributed to it") {
    auto e = expand("t = sub { file: \"lib/bad.sub\" }\n",
                    {{"patches/lib/bad.sub", "osc = = vco\n"}});
    REQUIRE_FALSE(e.errors.empty());
    CHECK(e.errors[0].file == "patches/lib/bad.sub");
  }
  SUBCASE("param without default") {
    auto e = expand("t = sub { file: \"lib/p.sub\" }\n",
                    {{"patches/lib/p.sub", "x = param {}\no = vco\n"}});
    REQUIRE_FALSE(e.errors.empty());
    CHECK(e.errors[0].msg.find("default") != std::string::npos);
  }
}

TEST_CASE("patch without subs is untouched, zero file reads") {
  int reads = 0;
  auto p = dsl::parse("osc = vco\nosc -> out.in\n");
  REQUIRE(p.ok());
  Diags d;
  auto r = graph::expand_subs(
      p.ast, "patches",
      [&](const std::string&, std::string&) { ++reads; return false; }, d);
  CHECK(d.empty());
  CHECK(reads == 0);
  CHECK(r.files.empty());
  CHECK(p.ast.modules.size() == 1);
}
