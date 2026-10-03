#include <doctest/doctest.h>
#include <cmath>
#include <numbers>
#include <limits>
#include <vector>
#include "dsl/parser.h"
#include "graph/graph.h"
#include "engine/engine.h"

using namespace ms;
namespace {
graph::CompileResult compile(const char* src) {
  auto p = dsl::parse(src);
  REQUIRE(p.ok());
  return graph::compile(p.ast, 48000);
}
std::vector<float> render(const char* src) {
  auto c = compile(src);
  REQUIRE(c.ok());
  engine::Engine e(std::move(c.graph));
  std::vector<float> l(48000), r(48000);
  e.process(l.data(), r.data(), l.size());
  return l;
}
double amplitude(const std::vector<float>& x, double f) {
  double re = 0, im = 0;
  for (size_t i = 0; i < x.size(); ++i) {
    const double phase = 2 * std::numbers::pi * f * static_cast<double>(i) / 48000;
    re += x[i] * std::cos(phase); im += x[i] * std::sin(phase);
  }
  return 2 * std::hypot(re, im) / static_cast<double>(x.size());
}
}  // namespace

TEST_CASE("additive reproduces stated partial amplitudes without an envelope or normalization") {
  auto x = render("a = additive { freq: 100, levels: [0.6, 0.3, -0.15] }\na -> out.in\n");
  CHECK(amplitude(x, 100) == doctest::Approx(0.6).epsilon(0.0001));
  CHECK(amplitude(x, 200) == doctest::Approx(0.3).epsilon(0.0001));
  CHECK(amplitude(x, 300) == doctest::Approx(0.15).epsilon(0.0001));
  CHECK(amplitude(x, 400) < 1e-6);
}

TEST_CASE("additive supports inharmonic ratios and spectral tilt") {
  auto x = render("a = additive { freq: 100, levels: [0.5, 0.5, 0.5], ratios: [1, 1.5, 2], tilt: -6.0206 }\na -> out.in\n");
  CHECK(amplitude(x, 100) == doctest::Approx(0.5).epsilon(0.0001));
  CHECK(amplitude(x, 150) == doctest::Approx(1.0/3).epsilon(0.0001));
  CHECK(amplitude(x, 200) == doctest::Approx(0.25).epsilon(0.0001));
}

TEST_CASE("additive removes out-of-band partials and fades near Nyquist") {
  auto x = render("a = additive { freq: 5000, levels: [0.2, 0.2], ratios: [1, 9] }\na -> out.in\n");
  CHECK(amplitude(x, 5000) == doctest::Approx(0.2).epsilon(0.0001));
  CHECK(amplitude(x, 3000) < 1e-6); // 45 kHz must not fold back to 3 kHz.
  auto near = render("a = additive { freq: 22800, levels: [0.4] }\na -> out.in\n");
  CHECK(amplitude(near, 22800) == doctest::Approx(0.2).epsilon(0.0001));
}

TEST_CASE("additive phase controls allow signed partial cancellation") {
  auto x = render("a = additive { freq: 100, levels: [1,1], ratios: [1,1], phases: [0,0.5] }\na -> out.in\n");
  double max = 0;
  for (float v : x) max = std::max(max, std::abs(static_cast<double>(v)));
  CHECK(max < 1e-6);
  auto dc = render("a = additive { freq: 0, levels: [0.6], phase: 0.25 }\na -> out.in\n");
  CHECK(dc.front() == doctest::Approx(0.6));
  CHECK(dc.back() == doctest::Approx(0.6));
}

TEST_CASE("additive validates partial arrays") {
  for (const char* src : {"a = additive { levels: [] }\n",
                         "a = additive { levels: [1,0.5], ratios: [1] }\n",
                         "a = additive { ratios: [0] }\n",
                         "a = additive { levels: [2] }\n",
                         "a = additive { phases: [0,1] }\n"}) {
    CAPTURE(src);
    CHECK_FALSE(compile(src).ok());
  }
}

TEST_CASE("default pitch agrees with absolute note literals") {
  auto pitched = render("a = vco {}\nA1v -> a.v_oct\na -> out.in\n");
  CHECK(amplitude(pitched, 55) == doctest::Approx(1.0).epsilon(0.0001));
  auto bank = render("a = additive {}\nA1v -> a.v_oct\na -> out.in\n");
  CHECK(amplitude(bank, 55) == doctest::Approx(1.0).epsilon(0.0001));
}

TEST_CASE("additive migrates partial phases and sync state and recovers from hostile controls") {
  const auto* info = dsp::find_module("additive");
  REQUIRE(info != nullptr);
  auto a = info->create(), b = info->create();
  a->init(48000, {}); b->init(48000, {});
  float ins[6] = {113, 0, 1, 0, 0.25f, 0}, oa[1], ob[1];
  for (int i = 0; i < 100; ++i) a->process(ins, oa);
  ins[5] = 1;
  a->process(ins, oa);
  b->copy_state_from(*a);
  for (int i = 0; i < 100; ++i) {
    a->process(ins, oa); b->process(ins, ob);
    CHECK(oa[0] == ob[0]);
  }
  for (int slot = 0; slot < 6; ++slot) {
    const float old = ins[slot];
    ins[slot] = std::numeric_limits<float>::quiet_NaN();
    a->process(ins, oa);
    CHECK(std::isfinite(oa[0]));
    ins[slot] = old;
    a->process(ins, oa);
    CHECK(std::isfinite(oa[0]));
  }
}

TEST_CASE("filter octave tracking matches the equivalent Hz cutoff") {
  for (const char* type : {"svf", "ladder"}) {
    const auto* info = dsp::find_module(type);
    auto a = info->create(), b = info->create();
    a->init(48000, {}); b->init(48000, {});
    std::vector<float> ia, ib, oa(info->outputs.size()), ob(info->outputs.size());
    for (auto in : info->inputs) ia.push_back(in.def);
    ib = ia;
    ia[1] = 800; ia.back() = -2;
    ib[1] = 200;
    for (int i = 0; i < 1000; ++i) {
      ia[0] = ib[0] = (i % 47 < 20) ? 0.2f : -0.2f;
      a->process(ia.data(), oa.data()); b->process(ib.data(), ob.data());
      CHECK(oa == ob);
    }
  }
}
