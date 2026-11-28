// Diagnostics a musician meets while writing a patch: every mistake reports
// once, at its own line:col, and names what was probably meant.
#include <string>
#include <vector>
#include <doctest/doctest.h>
#include "dsl/lexer.h"
#include "dsl/parser.h"
#include "dsl/suggest.h"
#include "graph/graph.h"
#include "graph/subs.h"

using namespace ms;

namespace {

// Every diagnostic from parse + subpatch expansion + compile, in order.
Diags diagnose(const char* src) {
  return graph::build_patch(src, "patch.patch", 48000.0).compiled.errors;
}

std::string only(const Diags& d) {
  REQUIRE(d.size() == 1);
  return d[0].msg;
}

bool has(const std::string& msg, const std::string& needle) {
  CAPTURE(msg);
  return msg.find(needle) != std::string::npos;
}

}  // namespace

TEST_CASE("closest_name: near misses, prefixes, and nothing for strangers") {
  const std::vector<std::string> ports{"in", "cutoff", "res", "v_oct"};
  CHECK(dsl::closest_name("cutof", ports) == "cutoff");
  CHECK(dsl::closest_name("input", ports) == "in");      // prefix
  CHECK(dsl::closest_name("resonance", ports) == "res");  // prefix
  CHECK(dsl::closest_name("voct", ports) == "v_oct");
  CHECK(dsl::closest_name("wobble", ports).empty());
  CHECK(dsl::closest_name("k", {"c", "kick"}).empty());  // one letter suggests nothing
  CHECK(dsl::did_you_mean("osx", {"osc"}) == " (did you mean 'osc'?)");
  CHECK(dsl::did_you_mean("zzz", {"osc"}, "!") == "!");
  CHECK(dsl::join({"a", "b", "c"}) == "a, b, c");
  CHECK(dsl::join({}).empty());
}

TEST_CASE("an unknown module type suggests the registered one, once") {
  const auto d = diagnose("osc = vcoo { freq: 110hz }\nosc -> out.in\n");
  const std::string msg = only(d);  // the cable to `osc` does not repeat it
  CHECK(has(msg, "unknown module type 'vcoo' (did you mean 'vco'?)"));
  CHECK(d[0].line == 1);
  CHECK(has(only(diagnose("f = filter\nf -> out.in\n")), "ratsnest describe"));
}

TEST_CASE("an unknown parameter names inputs and configs alike") {
  const std::string msg = only(diagnose("o = vco { waveform: saw }\no -> out.in\n"));
  CHECK(has(msg, "no parameter 'waveform' (did you mean 'wave'?)"));
  CHECK(has(msg, "phase, wave)"));  // configs are listed, not only inputs
}

TEST_CASE("enum values and lowercase notes say what was meant") {
  CHECK(has(only(diagnose("o = vco { wave: sawtooth }\no -> out.in\n")),
            "(did you mean 'saw'?)"));
  CHECK(has(only(diagnose("o = vco { freq: a4 }\no -> out.in\n")), "uppercase"));
}

TEST_CASE("ports: typos and cables written backwards") {
  const char* head = "o = vco\nf = svf\n";
  CHECK(has(only(diagnose((std::string(head) + "o -> f.cutof\nf -> out.in\n").c_str())),
            "no input 'cutof' (did you mean 'cutoff'?)"));
  CHECK(has(only(diagnose((std::string(head) + "o -> f.lp\n").c_str())),
            "'lp' is an output; cables run source -> destination"));
  CHECK(has(only(diagnose((std::string(head) + "o -> f.in\nf.cutoff -> out.in\n").c_str())),
            "'cutoff' is an input"));
  CHECK(has(only(diagnose("osc = vco\nosx -> out.in\n")), "unknown module 'osx' (did you mean 'osc'?)"));
}

TEST_CASE("lexer: uppercase units, '#' sharps, '=>' and capitalised names") {
  auto r = dsl::lex("freq: 440Hz, b: 2kHz");
  REQUIRE(r.errors.size() == 2);
  CHECK(has(r.errors[0].msg, "units are lowercase: hz"));
  CHECK(has(r.errors[1].msg, "units are lowercase: khz"));

  // `C#4`: one error, and '#' does not comment out the closing brace.
  auto p = dsl::parse("o = vco { freq: C#4 }\no -> out.in\n");
  REQUIRE(p.errors.size() == 1);
  CHECK(has(p.errors[0].msg, "'C#4' is not a note name"));
  CHECK(has(p.errors[0].msg, "Cs4"));

  auto a = dsl::lex("a => b");
  REQUIRE(a.errors.size() == 1);
  CHECK(has(a.errors[0].msg, "cables use '->'"));
  CHECK(a.tokens[1].kind == dsl::Tok::Arrow);

  auto n = dsl::parse("Osc = vco\nOsc -> out.in\n");
  REQUIRE(n.errors.size() == 2);  // one per spelling, no parser cascade
  CHECK(has(n.errors[0].msg, "names are lowercase"));

  CHECK(has(dsl::lex("my-osc").errors.at(0).msg, "names use '_'"));
}

TEST_CASE("parser: a missing comma or brace points at the open brace") {
  auto p = dsl::parse("o = vco { freq: 110hz wave: saw }\n");
  REQUIRE(p.errors.size() == 1);
  CHECK(has(p.errors[0].msg, "',' between parameters, or '}' to close the block opened at 1:9"));
  CHECK(p.errors[0].col == 23);
}

TEST_CASE("subpatch instances: unknown ports and params list the interface") {
  const char* tmpl =
      "td = subdef {}\n"
      "td.tune = param { default: 220 }\n"
      "td.trig = input {}\n"
      "td.main = output {}\n"
      "td.v = vco { freq: td.tune }\n"
      "td.trig -> td.v.sync\n"
      "td.v -> td.main.in\n";
  const std::string port = only(diagnose(
      (std::string(tmpl) + "c = clock\nk = sub { def: td }\nc -> k.trigger\nk -> out.in\n").c_str()));
  CHECK(has(port, "sub 'k' has no port 'trigger' (did you mean 'trig'?)"));
  CHECK(has(port, "(inputs: trig; outputs: main)"));
  // Internals stay addressable: `k.v` is a member, not a misspelled port.
  CHECK(diagnose((std::string(tmpl) + "k = sub { def: td }\n0.5 -> k.v.v_oct\nk -> out.in\n").c_str())
            .empty());
  const std::string param =
      only(diagnose((std::string(tmpl) + "k = sub { def: td, tunes: 3 }\nk -> out.in\n").c_str()));
  CHECK(has(param, "no param 'tunes' (did you mean 'tune'?) (params: tune)"));
}

TEST_CASE("a literal's unit must match what the input measures") {
  // hz, s and bpm scale by 1: without the check, each of these ran silently.
  CHECK(has(only(diagnose("o = vco { freq: 2s }\no -> out.in\n")), "expects a frequency"));
  CHECK(has(only(diagnose("e = adsr { attack: 440hz }\ne -> out.in\n")), "expects a time"));
  CHECK(has(only(diagnose("l = lfo { freq: 120bpm }\nl -> out.in\n")), "not a tempo"));
  CHECK(has(only(diagnose("o = vco { freq: A4v }\no -> out.in\n")), "not a v/oct pitch"));
  CHECK(has(only(diagnose("o = vco { v_oct: A4 }\no -> out.in\n")), "expects a v/oct pitch"));
  CHECK(has(only(diagnose("c = clock { bpm: 2hz }\nc -> out.in\n")), "expects a tempo"));
  const Diags cable = diagnose("o = vco\n3ms -> o.freq\no -> out.in\n");
  REQUIRE(cable.size() == 1);
  CHECK(cable[0].line == 2);
  CHECK(cable[0].col == 1);
  // Matching units, bare numbers, and inputs that declare no unit all pass.
  CHECK(diagnose("o = vco { freq: A4, v_oct: 0 }\ne = adsr { attack: 5ms }\n"
                 "c = clock { bpm: 120bpm }\nm = vca { gain: 2s }\n"
                 "440hz -> o.freq\nC3v -> o.v_oct\no -> out.in\n").empty());
}

TEST_CASE("units carry through subpatch params and input defaults") {
  const char* head =
      "v = subdef {}\n"
      "v.pitch = param { default: 220hz }\n"
      "v.decay = input { default: 0.2s }\n"
      "v.main = output {}\n"
      "v.osc = vco { freq: v.pitch }\n"
      "v.env = func\n";
  auto patch = [&](const char* rest) { return std::string(head) + rest; };
  // A param's default, and an override, land on a module param.
  CHECK(diagnose(patch("v.decay -> v.env.fall\nv.osc -> v.main.in\n"
                       "a = sub { def: v }\na.main -> out.in\n").c_str()).empty());
  CHECK(has(only(diagnose(patch("v.decay -> v.env.fall\nv.osc -> v.main.in\n"
                                "a = sub { def: v, pitch: 3s }\na.main -> out.in\n").c_str())),
            "expects a frequency"));
  // A param used as a cable source keeps its unit.
  CHECK(has(only(diagnose(patch("v.pitch -> v.env.rise\nv.decay -> v.env.fall\n"
                                "v.osc -> v.main.in\na = sub { def: v }\na.main -> out.in\n").c_str())),
            "expects a time"));
  // An unfed input's default drives its targets; a mismatch reports at the default.
  const Diags d = diagnose(patch("v.decay -> v.osc.freq\nv.osc -> v.main.in\n"
                                 "a = sub { def: v }\na.main -> out.in\n").c_str());
  REQUIRE(d.size() == 1);
  CHECK(has(d[0].msg, "expects a frequency"));
  CHECK(d[0].line == 3);
}
