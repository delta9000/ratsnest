// A patch should sound the same at every sample rate: `render --rate`, and
// above all ratsnest.clap, which runs at whatever rate the DAW is set to.
// Each probe here is a small patch rendered at 44.1, 48 and 96 kHz; its power
// below 16 kHz (what is audible and what both rates can carry) must agree
// with 48 kHz to kTol. Random sources draw a different sequence at each rate,
// so their probes run long enough to average many events.
#include <doctest/doctest.h>
#include <cmath>
#include <string>
#include <vector>
#include "dsp/biquad.h"
#include "engine/engine.h"
#include "graph/subs.h"

namespace {

constexpr double kTol = 0.5;  // dB

// Power below 16 kHz (two cascaded 2-pole lowpasses), in dB.
double audible_db(const char* patch, double rate, double seconds) {
  auto b = ms::graph::build_patch(patch, "probe.patch", rate);
  REQUIRE(b.compiled.ok());
  ms::engine::Engine e(std::move(b.compiled.graph));
  const size_t n = static_cast<size_t>(rate * seconds);
  std::vector<float> l(n), r(n);
  e.process(l.data(), r.data(), n);
  const auto c = ms::dsp::make_lpf(16000.0f, static_cast<float>(rate));
  ms::dsp::BiquadState s1, s2;
  double sq = 0.0;
  for (float x : l) {
    const double y = ms::dsp::Biquad::process(ms::dsp::Biquad::process(x, c, s1), c, s2);
    sq += y * y;
  }
  return 10.0 * std::log10(sq / double(n) + 1e-30);
}

void check_invariant(const char* name, const char* patch, double seconds, double tol = kTol) {
  CAPTURE(std::string(name));
  const double ref = audible_db(patch, 48000.0, seconds);
  for (double rate : {44100.0, 96000.0}) {
    CAPTURE(rate);
    CHECK(std::abs(audible_db(patch, rate, seconds) - ref) <= tol);
  }
}

}  // namespace

TEST_CASE("modal decays in seconds, not samples") {
  check_invariant("modal", R"(
c = clock { bpm: 30 }
m = modal { freq: 220hz, damping: 0.5 }
c -> m.strike
m -> out.in : 0.5
)", 4.0);
}

TEST_CASE("filtered noise keeps its level per Hz at every rate") {
  for (const char* color : {"white", "pink", "blue", "violet", "brown"}) {
    CAPTURE(std::string(color));
    const std::string p = std::string("n = noise { color: ") + color + R"( }
f = svf { cutoff: 1000hz, res: 0.9 }
n -> f.in
f.bp -> out.in : 0.5
)";
    // Violet's differencer already bends below its 6 dB/oct toward 24 kHz
    // at 48 kHz, which a 96 kHz differencer does not: its skirts read a
    // little hotter there.
    check_invariant("noise", p.c_str(), 4.0, std::string(color) == "violet" ? 0.75 : kTol);
  }
}

TEST_CASE("dust clicks carry the same energy at every rate") {
  check_invariant("dust", R"(
d = dust { rate: 200 }
f = svf { cutoff: 3000hz }
d -> f.in
f.lp -> out.in : 0.5
)", 8.0);
}

TEST_CASE("string plucks keep their level and tuning across rates") {
  // Many strikes over three octaves average out the pluck burst's noise.
  for (const char* damping : {"0.3", "0.6", "0.9"}) {
    CAPTURE(std::string(damping));
    const std::string p = std::string(R"(
c = clock { bpm: 480 }
r = random { seed: 3 }
4 -> r.rate
s = string { freq: 82hz, damping: )") + damping + R"( }
r -> s.v_oct : 1.5
c -> s.strike
s -> out.in : 0.5
)";
    check_invariant("string", p.c_str(), 20.0);
  }
}
