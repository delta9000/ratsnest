#include <doctest/doctest.h>
#include <cmath>
#include <vector>
#include "dsl/parser.h"
#include "engine/engine.h"
#include "graph/graph.h"

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
  CHECK(peak <= 1.0f);  // master clip respected
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
