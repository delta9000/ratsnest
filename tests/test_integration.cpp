#include <doctest/doctest.h>
#include <algorithm>
#include <cmath>
#include <filesystem>
#include <memory>
#include <vector>
#include "dsl/diag.h"
#include "dsl/parser.h"
#include "engine/engine.h"
#include "graph/graph.h"
#include "graph/subs.h"

using namespace ms;

static const char* kFirstPatch = R"(
# first light -- detuned saws with slow vibrato
osc1 = vco { freq: 110hz, wave: saw }
osc2 = vco { freq: 110.7hz, wave: saw }
vib  = lfo { freq: 0.3hz }
amp  = vca { gain: 0.4 }

vib  -> osc1.v_oct : 0.02
osc1 -> amp.in : 0.5
osc2 -> amp.in : 0.5
amp  -> out.in
)";

TEST_CASE("first.patch compiles and renders with sane levels") {
  auto p = dsl::parse(kFirstPatch);
  REQUIRE(p.ok());
  auto c = graph::compile(p.ast, 48000.0);
  REQUIRE(c.ok());
  engine::Engine e(std::move(c.graph));

  const size_t n = 48000;
  std::vector<float> l(n), r(n);
  e.process(l.data(), r.data(), n);

  double sum2 = 0.0;
  float peak = 0.0f;
  for (size_t i = 0; i < n; ++i) {
    sum2 += static_cast<double>(l[i]) * l[i];
    peak = std::max(peak, std::abs(l[i]));
  }
  const double rms = std::sqrt(sum2 / static_cast<double>(n));
  CHECK(rms > 0.05);    // audibly present
  CHECK(rms < 0.7);     // not slammed
  CHECK(peak <= 1.0f);  // this patch stays below unity
}

TEST_CASE("render is deterministic") {
  auto render_once = [] {
    auto p = dsl::parse(kFirstPatch);
    auto c = graph::compile(p.ast, 48000.0);
    REQUIRE(c.ok());
    engine::Engine e(std::move(c.graph));
    std::vector<float> l(4800), r(4800);
    e.process(l.data(), r.data(), 4800);
    return l;
  };
  CHECK(render_once() == render_once());
}

namespace {

double goertzel(const std::vector<float>& x, double hz, double rate = 48000.0) {
  const double w = 2.0 * 3.14159265358979323846 * hz / rate;
  const double coeff = 2.0 * std::cos(w);
  double s0 = 0.0, s1 = 0.0, s2 = 0.0;
  for (float v : x) {
    s0 = v + coeff * s1 - s2;
    s2 = s1;
    s1 = s0;
  }
  return std::sqrt(s1 * s1 + s2 * s2 - coeff * s1 * s2);
}
}  // namespace

TEST_CASE("two sub instances render two pitches; escape hatch retunes one") {
  static const char* kTone =
      "tune = param { default: 220 }\n"
      "main = output {}\n"
      "osc = vco { wave: sine, freq: tune }\n"
      "osc -> main.in\n";
  auto render = [&](const char* patch) {
    auto p = dsl::parse(patch);
    REQUIRE(p.ok());
    Diags d;
    graph::expand_subs(p.ast, "patches",
                        [&](const std::string& path, std::string& out) {
                          if (path != "patches/lib/tone.sub") return false;
                          out = kTone;
                          return true;
                        },
                        d);
    REQUIRE(d.empty());
    auto c = graph::compile(p.ast, 48000.0);
    REQUIRE(c.ok());
    engine::Engine e(std::move(c.graph));
    std::vector<float> l(48000), r(48000);
    e.process(l.data(), r.data(), l.size());
    return l;
  };

  auto x = render(
      "a = sub { file: \"lib/tone.sub\", tune: 220 }\n"
      "b = sub { file: \"lib/tone.sub\", tune: 330 }\n"
      "a -> out.in : 0.2\nb -> out.in : 0.2\n");
  CHECK(goertzel(x, 220.0) > 5.0 * goertzel(x, 275.0));
  CHECK(goertzel(x, 330.0) > 5.0 * goertzel(x, 275.0));

  auto y = render(
      "a = sub { file: \"lib/tone.sub\", tune: 220 }\n"
      "1 -> a.osc.v_oct\n"
      "a -> out.in : 0.2\n");
  CHECK(goertzel(y, 440.0) > 5.0 * goertzel(y, 220.0));
}

TEST_CASE("patches/lib/kick.sub compiles standalone and renders a kick") {
  namespace fs = std::filesystem;
  const auto root = fs::path(__FILE__).parent_path().parent_path();
  const auto path = (root / "patches" / "lib" / "kick.sub").string();
  std::string src;
  REQUIRE(graph::read_text_file(path, src));
  auto p = dsl::parse(src);
  REQUIRE(p.ok());
  Diags d;
  graph::expand_subs(p.ast, (root / "patches" / "lib").string(),
                     graph::read_text_file, d);
  REQUIRE(d.empty());
  auto c = graph::compile(p.ast, 48000.0);
  REQUIRE(c.ok());
  engine::Engine e(std::move(c.graph));
  std::vector<float> l(48000), r(48000);
  e.process(l.data(), r.data(), l.size());
  float peak = 0.0f;
  for (float v : l) peak = std::max(peak, std::abs(v));
  CHECK(peak > 0.05f);
  CHECK(peak <= 1.0f);
}

// poly-pad's `n` reaches `chord` unclamped (1..8), so every voice it can
// stamp needs a pitch input: p5..p8 must exist, or n > 4 drones on 0 v/oct.
TEST_CASE("patches/lib/poly-pad.sub takes a pitch per voice up to chord's eight") {
  namespace fs = std::filesystem;
  const auto root = fs::path(__FILE__).parent_path().parent_path();
  const std::string patch = (root / "patches" / "pp.patch").string();
  std::string src = "pad = sub { file: \"lib/poly-pad.sub\", n: 8 }\n1 -> pad.gate\n";
  for (int i = 1; i <= 8; ++i)
    src += "0." + std::to_string(i) + " -> pad.p" + std::to_string(i) + "\n";
  src += "pad -> out.in : 0.1\n";
  auto b = graph::build_patch(src, patch, 48000.0);
  REQUIRE_MESSAGE(b.compiled.ok(), b.compiled.errors[0].msg);
  // Each voice's pitch reaches its own stamped synth-voice.
  engine::Engine e(std::move(b.compiled.graph));
  std::vector<float> l(64), r(64);
  e.process(l.data(), r.data(), l.size());
  const graph::Graph& g = e.graph();
  int voices = 0;
  for (size_t i = 0; i < g.nodes.size(); ++i)
    if (g.nodes[i].info->type == std::string("chord"))
      for (int v = 0; v < 8; ++v)
        voices += e.output_value(i, static_cast<size_t>(8 + v)) == doctest::Approx(0.1 * (v + 1));
  CHECK(voices == 8);
}

// `render --start` runs the graph from zero and throws the skipped samples
// away -- a stateful graph has no seek. That is only honest if block size
// cannot change the result, so pin it: a chunked skip followed by a capture
// must be bit-identical to the tail of one long pass.
TEST_CASE("chunked skip then capture equals the tail of one pass") {
  auto build = [] {
    auto p = dsl::parse(kFirstPatch);
    REQUIRE(p.ok());
    auto c = graph::compile(p.ast, 48000.0);
    REQUIRE(c.ok());
    return std::make_unique<engine::Engine>(std::move(c.graph));
  };
  const size_t skip = 3000, keep = 1024;  // skip is not a chunk multiple

  auto full = build();
  std::vector<float> fl(skip + keep), fr(skip + keep);
  full->process(fl.data(), fr.data(), skip + keep);

  auto offset = build();
  std::vector<float> sl(256), sr(256);
  for (size_t done = 0; done < skip;) {
    const size_t n = std::min<size_t>(256, skip - done);
    offset->process(sl.data(), sr.data(), n);
    done += n;
  }
  std::vector<float> ol(keep), orr(keep);
  offset->process(ol.data(), orr.data(), keep);

  CHECK(std::vector<float>(fl.begin() + static_cast<long>(skip), fl.end()) == ol);
  CHECK(std::vector<float>(fr.begin() + static_cast<long>(skip), fr.end()) == orr);
}
