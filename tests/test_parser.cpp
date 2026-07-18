#include <doctest/doctest.h>
#include "dsl/parser.h"

using namespace ms::dsl;

TEST_CASE("parses declarations with params") {
  auto r = parse("filt = ladder { cutoff: 800hz, res: 0.45 }\n");
  REQUIRE(r.ok());
  REQUIRE(r.ast.modules.size() == 1);
  auto& m = r.ast.modules[0];
  CHECK(m.name == "filt");
  CHECK(m.type == "ladder");
  REQUIRE(m.params.size() == 2);
  CHECK(m.params[0].name == "cutoff");
  CHECK(m.params[0].value.num == doctest::Approx(800.0));
  CHECK(m.params[1].value.kind == Value::Kind::Num);
}

TEST_CASE("declaration without params") {
  auto r = parse("amp = vca");
  REQUIRE(r.ok());
  CHECK(r.ast.modules[0].type == "vca");
}

TEST_CASE("parses cables with ports and depth") {
  auto r = parse("lfo1 -> filt.cutoff : 0.5\nosc -> amp\n");
  REQUIRE(r.ok());
  REQUIRE(r.ast.cables.size() == 2);
  CHECK(r.ast.cables[0].src.module == "lfo1");
  CHECK(r.ast.cables[0].src.port.empty());
  CHECK(r.ast.cables[0].dst.module == "filt.cutoff");  // graph splits, not parser
  CHECK(r.ast.cables[0].dst.port.empty());
  CHECK(r.ast.cables[0].depth == doctest::Approx(0.5));
  CHECK(r.ast.cables[1].depth == doctest::Approx(1.0));
}

TEST_CASE("literal as cable source") {
  auto r = parse("0.25 -> qnt.transpose");
  REQUIRE(r.ok());
  CHECK(r.ast.cables[0].src.is_literal);
  CHECK(r.ast.cables[0].src.literal == doctest::Approx(0.25));
}

TEST_CASE("array and enum and string values") {
  auto r = parse("q = quant { scale: [0, 3, 5], root: c }\ns = sampler { file: \"a.wav\" }");
  REQUIRE(r.ok());
  auto& scale = r.ast.modules[0].params[0].value;
  REQUIRE(scale.kind == Value::Kind::Array);
  REQUIRE(scale.items.size() == 3);
  CHECK(scale.items[2].num == doctest::Approx(5.0));
  CHECK(r.ast.modules[0].params[1].value.kind == Value::Kind::Ident);
  CHECK(r.ast.modules[1].params[0].value.kind == Value::Kind::Str);
}

TEST_CASE("recovers and reports multiple errors with positions") {
  auto r = parse("osc = \nx -> \nok = vca\n");
  REQUIRE(r.errors.size() == 2);
  CHECK(r.errors[0].line == 1);
  CHECK(r.errors[1].line == 2);
  CHECK(r.ast.modules.size() == 1);  // 'ok' still parsed
}

TEST_CASE("duplicate module name is an error") {
  auto r = parse("a = vca\na = vca\n");
  REQUIRE(r.errors.size() == 1);
  CHECK(r.errors[0].msg.find("duplicate") != std::string::npos);
}

TEST_CASE("dotted declaration names parse") {
  auto r = parse("lead.osc = vco { wave: saw }\n");
  REQUIRE(r.ok());
  CHECK(r.ast.modules[0].name == "lead.osc");
}

TEST_CASE("dotted cable endpoints keep the full chain") {
  auto r = parse("a.b = vca\nc = vca\na.b -> c.cv\nc -> a.b.cv\n");
  REQUIRE(r.ok());
  CHECK(r.ast.cables[0].src.module == "a.b");
  CHECK(r.ast.cables[0].src.port.empty());
  CHECK(r.ast.cables[1].dst.module == "a.b.cv");  // graph splits, not parser
}

TEST_CASE("cable depth accepts an identifier") {
  auto r = parse("a = vca\nb = vca\na -> b.in : click\n");
  REQUIRE(r.ok());
  REQUIRE(r.ast.cables.size() == 1);
  CHECK(r.ast.cables[0].depth_ident == "click");
  CHECK(r.ast.cables[0].depth == doctest::Approx(1.0));
}

TEST_CASE("numeric cable depth leaves depth_ident empty") {
  auto r = parse("a = vca\nb = vca\na -> b.in : 0.5\n");
  REQUIRE(r.ok());
  CHECK(r.ast.cables[0].depth_ident.empty());
  CHECK(r.ast.cables[0].depth == doctest::Approx(0.5));
}
