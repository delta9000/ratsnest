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

static const char* kVoiceSub = R"(
gain = param { default: 0.5 }
trig = input {}
lvl  = input { default: 0.25 }
main = output {}
aux  = output {}
env  = func { rise: 0.001s, fall: 0.1s }
trig -> env.trig : 2.0
osc  = vco { wave: sine, freq: 110 }
amp  = vca { gain: 0 }
osc -> amp.in
env -> amp.cv
lvl -> amp.cv : 0.5
amp -> main.in : 0.9
osc -> aux.in
)";

TEST_CASE("ports: outside feeds fan out, reads fan in, depths multiply") {
  auto e = expand(
      "clk = clock\n"
      "v = sub { file: \"lib/v.sub\" }\n"
      "clk -> v.trig : 3.0\n"
      "v.main -> out.in : 2.0\n"
      "v.aux -> out.in\n",
      {{"patches/lib/v.sub", kVoiceSub}});
  REQUIRE(e.errors.empty());
  CHECK_FALSE(find_mod(e.ast, "v.trig"));
  CHECK_FALSE(find_mod(e.ast, "v.main"));
  const auto* feed = find_cable_to(e.ast, "v.env.trig");
  REQUIRE(feed);
  CHECK(feed->src.module == "clk");
  CHECK(feed->depth == doctest::Approx(6.0));
  bool found = false;
  for (const auto& c : e.ast.cables)
    if (!c.src.is_literal && c.src.module == "v.amp" && c.dst.module == "out.in") {
      CHECK(c.depth == doctest::Approx(1.8));
      found = true;
    }
  CHECK(found);
  CHECK(has_cable(e.ast, "v.osc", "out.in"));
}

TEST_CASE("ports: unpatched input contributes its default as a literal") {
  auto e = expand("v = sub { file: \"lib/v.sub\" }\nv.main -> out.in\n",
                  {{"patches/lib/v.sub", kVoiceSub}});
  REQUIRE(e.errors.empty());
  bool found = false;
  for (const auto& c : e.ast.cables)
    if (c.src.is_literal && c.dst.module == "v.amp.cv") {
      CHECK(c.src.literal == doctest::Approx(0.25));
      CHECK(c.depth == doctest::Approx(0.5));
      found = true;
    }
  CHECK(found);
  CHECK_FALSE(find_cable_to(e.ast, "v.env.trig"));
}

TEST_CASE("ports: bare instance reads the first declared output") {
  auto e = expand("v = sub { file: \"lib/v.sub\" }\nv -> out.in\n",
                  {{"patches/lib/v.sub", kVoiceSub}});
  REQUIRE(e.errors.empty());
  CHECK(has_cable(e.ast, "v.amp", "out.in"));
}

TEST_CASE("ports: misuse is diagnosed") {
  std::map<std::string, std::string> files{{"patches/lib/v.sub", kVoiceSub}};
  SUBCASE("output as destination") {
    auto e = expand("v = sub { file: \"lib/v.sub\" }\nclk = clock\nclk -> v.main\n", files);
    REQUIRE_FALSE(e.errors.empty());
    CHECK(e.errors[0].msg.find("main") != std::string::npos);
  }
  SUBCASE("input as source") {
    auto e = expand("v = sub { file: \"lib/v.sub\" }\nv.trig -> out.in\n", files);
    REQUIRE_FALSE(e.errors.empty());
  }
  SUBCASE("port has no members") {
    auto e = expand("v = sub { file: \"lib/v.sub\" }\nclk = clock\nclk -> v.trig.x\n", files);
    REQUIRE_FALSE(e.errors.empty());
  }
  SUBCASE("input wired straight to output inside the body") {
    auto e = expand("v = sub { file: \"lib/w.sub\" }\n",
                    {{"patches/lib/w.sub",
                      "i = input {}\no = output {}\ni -> o.in\n"}});
    REQUIRE_FALSE(e.errors.empty());
  }
}

TEST_CASE("escape hatch: cables into stamped internals pass through") {
  auto e = expand(
      "v = sub { file: \"lib/v.sub\" }\n"
      "0.1 -> v.osc.v_oct\n"
      "v.main -> out.in\n",
      {{"patches/lib/v.sub", kVoiceSub}});
  REQUIRE(e.errors.empty());
  const auto* c = find_cable_to(e.ast, "v.osc.v_oct");
  REQUIRE(c);
  CHECK(c->src.is_literal);
}

TEST_CASE("subdef: inline template instantiates twice with different params") {
  auto e = expand(
      "td = subdef {}\n"
      "td.tune = param { default: 110 }\n"
      "td.trig = input {}\n"
      "td.main = output {}\n"
      "td.osc = vco { wave: sine, freq: td.tune }\n"
      "td.env = func { rise: 0.001s, fall: 0.2s }\n"
      "td.trig -> td.env.trig\n"
      "td.osc -> td.main.in\n"
      "t1 = sub { def: td, tune: 220 }\n"
      "t2 = sub { def: td }\n"
      "t1.main -> out.in\n"
      "t2.main -> out.in\n");
  REQUIRE(e.errors.empty());
  CHECK_FALSE(find_mod(e.ast, "td"));
  CHECK_FALSE(find_mod(e.ast, "td.osc"));
  CHECK(find_cfg(*find_mod(e.ast, "t1.osc"), "freq")->value.num == doctest::Approx(220.0));
  CHECK(find_cfg(*find_mod(e.ast, "t2.osc"), "freq")->value.num == doctest::Approx(110.0));
  CHECK(has_cable(e.ast, "t1.osc", "out.in"));
  CHECK(has_cable(e.ast, "t2.osc", "out.in"));
}

TEST_CASE("subdef: errors") {
  SUBCASE("unknown def name") {
    auto e = expand("t = sub { def: nope }\n");
    REQUIRE_FALSE(e.errors.empty());
    CHECK(e.errors[0].msg.find("nope") != std::string::npos);
  }
  SUBCASE("dotted subdef name") {
    auto e = expand("a.b = subdef {}\na.b.x = vco\n");
    REQUIRE_FALSE(e.errors.empty());
  }
  SUBCASE("cable crossing the template boundary") {
    auto e = expand(
        "td = subdef {}\n"
        "td.osc = vco\n"
        "lfo1 = lfo\n"
        "lfo1 -> td.osc.fm\n"
        "t = sub { def: td }\n");
    REQUIRE_FALSE(e.errors.empty());
  }
  SUBCASE("subdef with no members") {
    auto e = expand("td = subdef {}\nt = sub { def: td }\n");
    REQUIRE_FALSE(e.errors.empty());
  }
}

TEST_CASE("nesting: sub files include subs, paths resolve per file") {
  auto e = expand(
      "k = sub { file: \"lib/kit.sub\", tune: 330 }\nk.main -> out.in\n",
      {{"patches/lib/kit.sub",
        "tune = param { default: 110 }\n"
        "main = output {}\n"
        "inner = sub { file: \"tone.sub\", tune: tune }\n"
        "inner.main -> main.in\n"},
       {"patches/lib/tone.sub",
        "tune = param { default: 55 }\n"
        "main = output {}\n"
        "osc = vco { wave: sine, freq: tune }\n"
        "osc -> main.in\n"}});
  REQUIRE(e.errors.empty());
  const auto* osc = find_mod(e.ast, "k.inner.osc");
  REQUIRE(osc);
  CHECK(find_cfg(*osc, "freq")->value.num == doctest::Approx(330.0));
  CHECK(has_cable(e.ast, "k.inner.osc", "out.in"));
  REQUIRE(e.files.size() == 2);
  CHECK(e.files[0] == "patches/lib/kit.sub");
  CHECK(e.files[1] == "patches/lib/tone.sub");
}

TEST_CASE("nesting: include cycle is a clean error with the chain") {
  auto e = expand("a = sub { file: \"lib/a.sub\" }\n",
                  {{"patches/lib/a.sub", "x = sub { file: \"b.sub\" }\n"},
                   {"patches/lib/b.sub", "y = sub { file: \"a.sub\" }\n"}});
  REQUIRE_FALSE(e.errors.empty());
  CHECK(e.errors[0].msg.find("cycle") != std::string::npos);
  CHECK(e.errors[0].msg.find("via") != std::string::npos);
}

TEST_CASE("nesting: self-include reports a cycle, depth cap holds") {
  auto e = expand("a = sub { file: \"lib/loop.sub\" }\n",
                  {{"patches/lib/loop.sub", "x = sub { file: \"loop.sub\" }\n"}});
  REQUIRE_FALSE(e.errors.empty());
  CHECK(e.errors[0].msg.find("cycle") != std::string::npos);
}

static const char* kDemoSub = R"(
tune = param { default: 110 }
trig = input {}
main = output {}
env = func { rise: 0.001s, fall: 0.1s }
trig -> env.trig
osc = vco { wave: sine, freq: tune }
osc -> main.in
demo.clk = clock { bpm: 120 }
demo.clk -> trig
main -> demo.out.in : 0.8
)";

TEST_CASE("demo: dropped on instantiation") {
  auto e = expand("v = sub { file: \"lib/d.sub\" }\nv.main -> out.in\n",
                  {{"patches/lib/d.sub", kDemoSub}});
  REQUIRE(e.errors.empty());
  CHECK_FALSE(find_mod(e.ast, "v.demo.clk"));
  for (const auto& c : e.ast.cables) {
    CHECK(c.dst.module.find("demo") == std::string::npos);
    if (!c.src.is_literal) CHECK(c.src.module.find("demo") == std::string::npos);
  }
}

TEST_CASE("demo: top-level compile is an implicit instantiation with defaults") {
  auto e = expand(kDemoSub);
  REQUIRE(e.errors.empty());
  CHECK(find_cfg(*find_mod(e.ast, "osc"), "freq")->value.num == doctest::Approx(110.0));
  CHECK_FALSE(find_mod(e.ast, "trig"));
  CHECK_FALSE(find_mod(e.ast, "main"));
  CHECK(find_mod(e.ast, "demo.clk"));
  CHECK(has_cable(e.ast, "demo.clk", "env.trig"));
  bool to_out = false;
  for (const auto& c : e.ast.cables)
    if (!c.src.is_literal && c.src.module == "osc" && c.dst.module == "out.in") {
      CHECK(c.depth == doctest::Approx(0.8));
      to_out = true;
    }
  CHECK(to_out);
}

TEST_CASE("demo: out misuse in a subpatch file") {
  SUBCASE("non-demo out reference") {
    auto e = expand("i = input {}\nosc = vco\nosc -> out.in\n");
    REQUIRE_FALSE(e.errors.empty());
    CHECK(e.errors[0].msg.find("out") != std::string::npos);
  }
  SUBCASE("interface pseudo-module under demo prefix") {
    auto e = expand("main = output {}\ndemo.x = input {}\nosc = vco\nosc -> main.in\n");
    REQUIRE_FALSE(e.errors.empty());
  }
}
