// Regressions found by mutation-fuzzing `ratsnest check` over every patch in
// patches/ (design rule 6: a bad patch reports line:col, never crashes,
// hangs, or slips through silently).
#include <filesystem>
#include <map>
#include <string>
#include <doctest/doctest.h>
#include "dsl/lexer.h"
#include "dsl/parser.h"
#include "graph/graph.h"
#include "graph/subs.h"
#include "graph/voices.h"

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

Diags expand(const std::string& src, std::map<std::string, std::string> files) {
  auto p = dsl::parse(src);
  REQUIRE(p.ok());
  Diags d;
  graph::expand_subs(p.ast, "patches", mem(std::move(files)), d);
  return d;
}

bool has(const Diags& d, const std::string& text) {
  for (const auto& e : d)
    if (e.msg.find(text) != std::string::npos) return true;
  return false;
}

}  // namespace

TEST_CASE("deeply nested arrays report instead of overflowing the stack") {
  const std::string src = "s = stepseq { steps: " + std::string(200000, '[') + "1 }\n";
  auto r = dsl::parse(src);
  REQUIRE_FALSE(r.ok());
  CHECK(r.errors[0].msg.find("nested deeper") != std::string::npos);
  CHECK(r.errors[0].line == 1);
  CHECK(r.errors[0].col > 1);
  // Reasonable nesting still parses.
  CHECK(dsl::parse("s = stepseq { steps: [[[1, 2], [3]]] }\n").ok());
}

TEST_CASE("a stray control byte is escaped so the diagnostic stays one line") {
  auto r = dsl::lex("osc = vco\n\x1c\n");
  REQUIRE(r.errors.size() == 1);
  CHECK(r.errors[0].msg == "unexpected character '\\x1c'");
  CHECK(r.errors[0].line == 2);
  CHECK(r.errors[0].col == 1);
}

TEST_CASE("an out-of-range voice count saturates instead of overflowing int") {
  // static_cast<int>(1e11) is undefined behaviour; the count must clamp to the bank width (16).
  auto p = dsl::parse(
      "keys = alloc { n: 99999999999 }\n"
      "lead = voice { notes: keys }\n"
      "lead.osc = vco\n"
      "keys.voct -> lead.osc.v_oct\n");
  REQUIRE(p.ok());
  Diags d;
  graph::expand_voices(p.ast, d);
  REQUIRE(d.empty());
  CHECK(p.ast.modules.size() == 1 + 16);

  auto q = dsl::parse(
      "keys = alloc { n: 3 }\n"
      "lead = voice { notes: keys, n: 99999999999 }\n"
      "lead.osc = vco\n");
  REQUIRE(q.ok());
  Diags e;
  graph::expand_voices(q.ast, e);
  REQUIRE(e.size() == 1);
  CHECK(e[0].msg.find("does not match") != std::string::npos);
  CHECK(e[0].msg.find("-") == std::string::npos);  // no wrapped negative count
}

TEST_CASE("a subpatch path that is a directory cannot be read") {
  std::string out;
  CHECK_FALSE(graph::read_text_file(std::filesystem::temp_directory_path().string(), out));
}

TEST_CASE("parent-side splice errors name the parent's file, even nested") {
  // b.sub's `w` has no outputs; the bad read `w -> o.in` is written in a.sub.
  auto d = expand("x = sub { file: \"a.sub\" }\nx -> out.in\n",
                  {{"patches/a.sub", "o = output {}\nw = sub { file: \"b.sub\" }\nw -> o.in\n"},
                   {"patches/b.sub", "osc = vco\n"}});
  REQUIRE_FALSE(d.empty());
  CHECK(has(d, "has no outputs"));
  CHECK(d[0].file == "patches/a.sub");
  CHECK(d[0].line == 3);

  // Same for an output that is never driven, one level down.
  d = expand("x = sub { file: \"a.sub\" }\nx -> out.in\n",
             {{"patches/a.sub", "o = output {}\nw = sub { file: \"b.sub\" }\nw -> o.in\n"},
              {"patches/b.sub", "m = output {}\nosc = vco\n"}});
  REQUIRE_FALSE(d.empty());
  CHECK(has(d, "never driven"));
  CHECK(d[0].file == "patches/a.sub");
  CHECK(d[0].line == 3);
}

TEST_CASE("runaway subpatch fan-out reports instead of hanging") {
  SUBCASE("modules: each level instantiates its child four times") {
    std::map<std::string, std::string> files;
    for (int i = 0; i <= 10; ++i) {
      std::string s = "o = output {}\nm = mix\nm -> o.in\n";
      if (i < 10)
        for (int k = 1; k <= 4; ++k)
          s += "s" + std::to_string(k) + " = sub { file: \"l" + std::to_string(i + 1) +
               ".sub\" }\ns" + std::to_string(k) + " -> m.in" + std::to_string(k) + "\n";
      files["patches/l" + std::to_string(i) + ".sub"] = s;
    }
    auto d = expand("a = sub { file: \"l0.sub\" }\na -> out.in\n", files);
    REQUIRE_FALSE(d.empty());
    CHECK(has(d, "expansion exceeds"));
    CHECK(d[0].line > 0);
    CHECK(d[0].col > 0);
  }
  SUBCASE("cables: an input fanned out four ways at every level") {
    std::map<std::string, std::string> files;
    for (int i = 0; i <= 12; ++i) {
      std::string s = "i = input {}\no = output {}\nm = mix\nm -> o.in\n";
      for (int k = 1; k <= 4; ++k) s += "i -> m.in" + std::to_string(k) + "\n";
      if (i < 12) {
        s += "s = sub { file: \"c" + std::to_string(i + 1) + ".sub\" }\n";
        for (int k = 1; k <= 4; ++k) s += "i -> s.i\n";
      }
      files["patches/c" + std::to_string(i) + ".sub"] = s;
    }
    auto d = expand("n = noise\na = sub { file: \"c0.sub\" }\nn -> a.i\na -> out.in\n", files);
    REQUIRE_FALSE(d.empty());
    CHECK(has(d, "expansion exceeds"));
    CHECK(d[0].line > 0);
    CHECK(d[0].col > 0);
  }
}

TEST_CASE("a subdef's interface inside a .sub stays the template's, not the file's") {
  // Checks clean as a top-level file; must also expand clean when included.
  const char* lib =
      "td = subdef {}\n"
      "td.tune = param { default: 220 }\n"
      "td.o = output {}\n"
      "td.v = vco { freq: td.tune }\n"
      "td.v -> td.o.in\n"
      "t = sub { def: td }\n"
      "main = output {}\n"
      "t -> main.in\n";
  auto d = expand("x = sub { file: \"lib.sub\" }\nx -> out.in\n", {{"patches/lib.sub", lib}});
  CHECK(d.empty());
  // And the template's param is not an override key of the including sub.
  auto p = dsl::parse("x = sub { file: \"lib.sub\", tune: 3 }\nx -> out.in\n");
  REQUIRE(p.ok());
  Diags e;
  graph::expand_subs(p.ast, "patches", mem({{"patches/lib.sub", lib}}), e);
  CHECK(has(e, "has no param 'tune'"));
}

TEST_CASE("an empty subdef reports a real column") {
  auto d = expand("x = vco\n  td = subdef {}\n", {});
  REQUIRE(d.size() == 1);
  CHECK(has(d, "has no members"));
  CHECK(d[0].line == 2);
  CHECK(d[0].col == 3);
}

TEST_CASE("a declared name colliding with a stamped subpatch module is an error") {
  auto p = dsl::parse("t = sub { file: \"t.sub\" }\nt.osc = vco\nt -> out.in\n");
  REQUIRE(p.ok());
  Diags d;
  graph::expand_subs(p.ast, "patches",
                     mem({{"patches/t.sub", "o = output {}\nosc = vco\nosc -> o.in\n"}}), d);
  REQUIRE(d.empty());
  auto c = graph::compile(p.ast, 48000.0);
  REQUIRE_FALSE(c.ok());
  CHECK(c.errors[0].msg.find("duplicate module name 't.osc'") != std::string::npos);
  CHECK(c.errors[0].line > 0);
}
