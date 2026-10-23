#include <doctest/doctest.h>
#include <algorithm>
#include <cmath>
#include <limits>
#include <numbers>
#include <string>
#include <vector>
#include "dsp/modules.h"

using namespace ms::dsp;

namespace {
Config model_cfg(const std::string& model) {
  Config cfg;
  ConfigValue v;
  v.str = model;
  cfg.emplace_back("model", v);
  return cfg;
}

// in, drive, tone, level, mix
struct OdRig {
  std::unique_ptr<Module> module;
  float ins[5] = {0.f, 0.5f, 0.5f, 0.5f, 1.0f};
  float outs[1] = {};

  explicit OdRig(const std::string& model = "tubescreamer") {
    const ModuleInfo* info = find_module("overdrive");
    REQUIRE(info != nullptr);
    REQUIRE(info->input_index("mix") == 4);
    REQUIRE(info->output_index("out") == 0);
    module = info->create();
    module->init(48000.0, model_cfg(model));
  }

  float tick() {
    module->process(ins, outs);
    return outs[0];
  }

  // Drive a sine of the given frequency/amp for n samples, return the output.
  std::vector<float> run_sine(double hz, float amp, int n) {
    std::vector<float> out(static_cast<std::size_t>(n));
    for (int i = 0; i < n; ++i) {
      ins[0] = static_cast<float>(amp * std::sin(2.0 * std::numbers::pi * hz *
                                                 static_cast<double>(i) /
                                                 48000.0));
      out[static_cast<std::size_t>(i)] = tick();
    }
    return out;
  }
};

double goertzel(const std::vector<float>& x, double hz, std::size_t from) {
  const double w = 2.0 * std::numbers::pi * hz / 48000.0;
  const double c = 2.0 * std::cos(w);
  double s1 = 0.0, s2 = 0.0;
  for (std::size_t i = from; i < x.size(); ++i) {
    const double s0 = static_cast<double>(x[i]) + c * s1 - s2;
    s2 = s1;
    s1 = s0;
  }
  return std::sqrt(std::max(s1 * s1 + s2 * s2 - c * s1 * s2, 0.0));
}
}  // namespace

TEST_CASE("overdrive is exactly silent without input") {
  OdRig od;
  for (int i = 0; i < 4800; ++i) CHECK(od.tick() == 0.0f);
}

TEST_CASE("overdrive drive raises harmonic content") {
  auto harmonics = [](float drive) {
    OdRig od;
    od.ins[1] = drive;
    const auto x = od.run_sine(220.0, 0.3f, 16384);
    return goertzel(x, 440.0, 2048) + goertzel(x, 660.0, 2048);
  };
  CHECK(harmonics(0.8f) > 3.0 * harmonics(0.2f));
}

TEST_CASE("overdrive mix 0 is a linear path (no added harmonics)") {
  OdRig od("rat");
  od.ins[1] = 0.9f;   // high drive
  od.ins[4] = 0.0f;   // fully clean path
  const auto x = od.run_sine(220.0, 0.5f, 16384);
  const double fund = goertzel(x, 220.0, 2048);
  const double h2 = goertzel(x, 440.0, 2048);
  const double h3 = goertzel(x, 660.0, 2048);
  CHECK((h2 + h3) < 0.02 * fund);   // pure sine stays a sine
}

TEST_CASE("overdrive mix 1 adds strong harmonics") {
  OdRig od("rat");
  od.ins[1] = 0.9f;
  od.ins[4] = 1.0f;
  const auto x = od.run_sine(220.0, 0.5f, 16384);
  const double fund = goertzel(x, 220.0, 2048);
  const double h3 = goertzel(x, 660.0, 2048);
  CHECK(h3 > 0.05 * fund);
}

TEST_CASE("overdrive models differ (soft vs hard clip structure)") {
  OdRig ts("tubescreamer"), rat("rat");
  ts.ins[1] = rat.ins[1] = 0.7f;
  const auto xt = ts.run_sine(220.0, 0.5f, 16384);
  const auto xr = rat.run_sine(220.0, 0.5f, 16384);
  // Hard clip generates a richer high-harmonic tail than the soft diode.
  const double ts_h5 = goertzel(xt, 1100.0, 2048) / goertzel(xt, 220.0, 2048);
  const double rat_h5 = goertzel(xr, 1100.0, 2048) / goertzel(xr, 220.0, 2048);
  CHECK(rat_h5 > ts_h5);
}

TEST_CASE("overdrive tone opens up the high end") {
  // tubescreamer tone corner sweeps ~2 kHz (tone 0) -> ~6 kHz (tone 1); probe
  // the 8th harmonic at 4 kHz, which the low setting attenuates.
  auto high_ratio = [](float tone) {
    OdRig od("tubescreamer");
    od.ins[1] = 0.8f;
    od.ins[2] = tone;
    const auto x = od.run_sine(500.0, 0.5f, 16384);
    return goertzel(x, 4000.0, 2048) / goertzel(x, 500.0, 2048);
  };
  CHECK(high_ratio(1.0f) > high_ratio(0.0f));
}

TEST_CASE("overdrive suppresses aliasing on a high tone") {
  // 5 kHz full-scale into a hard clipper: without antialiasing the 9th
  // harmonic (45 kHz) folds to a strong inharmonic bin at 3 kHz. ADAA + 4x
  // oversampling keeps that alias far below the fundamental. tone=1 puts the
  // tone corner (~6 kHz) above the 5 kHz fundamental so the filter does not
  // skew the comparison (3 kHz is not a harmonic of 5 kHz).
  OdRig od("rat");
  od.ins[1] = 0.9f;   // high drive
  od.ins[2] = 1.0f;   // tone wide open (~6 kHz corner)
  const auto x = od.run_sine(5000.0, 1.0f, 16384);
  const double fund = goertzel(x, 5000.0, 2048);
  const double alias = goertzel(x, 3000.0, 2048);  // 9th-harmonic fold
  CHECK(alias < 0.1 * fund);
}

TEST_CASE("overdrive survives hostile inputs and recovers") {
  OdRig od;
  od.ins[0] = std::numeric_limits<float>::quiet_NaN();
  od.ins[1] = 1e9f;
  od.ins[2] = std::numeric_limits<float>::infinity();
  for (int i = 0; i < 512; ++i) od.tick();
  od.ins[1] = 0.5f;
  od.ins[2] = 0.5f;
  float last = 0.0f;
  for (int i = 0; i < 4800; ++i) {
    od.ins[0] = 0.3f * std::sin(0.05f * static_cast<float>(i));
    last = od.tick();
  }
  CHECK(std::isfinite(last));
}

TEST_CASE("overdrive state migrates across instances") {
  OdRig a, b;
  for (int i = 0; i < 2000; ++i) {
    a.ins[0] = 0.4f * std::sin(0.05f * static_cast<float>(i));
    a.tick();
  }
  b.module->copy_state_from(*a.module);
  for (int i = 2000; i < 2400; ++i) {
    const float s = 0.4f * std::sin(0.05f * static_cast<float>(i));
    a.ins[0] = s;
    b.ins[0] = s;
    const float ya = a.tick();
    const float yb = b.tick();
    CHECK(ya == doctest::Approx(yb).epsilon(1e-4));
  }
}

// Drive is read at block rate (32 samples); a stepped pre-gain into the
// shaper buzzes at rate/32 under modulation. The per-sample ramp spreads each
// change across the block, so no block phase carries a larger jump. (Input
// and LFO rates are chosen incommensurate with the 32-sample block.)
TEST_CASE("overdrive drive modulation does not zipper at block boundaries") {
  OdRig rig("tubescreamer");
  double phase_d1[32] = {};
  double y1 = 0.0;
  for (int i = 0; i < 48000; ++i) {
    const double t = static_cast<double>(i) / 48000.0;
    rig.ins[0] = static_cast<float>(0.3 * std::sin(2.0 * std::numbers::pi * 110.3 * t));
    rig.ins[1] = static_cast<float>(0.5 + 0.5 * std::sin(2.0 * std::numbers::pi * 2.9 * t));
    const double y = rig.tick();
    if (i > 100) phase_d1[i % 32] += std::abs(y - y1);
    y1 = y;
  }
  double peak = 0.0, mean = 0.0;
  for (double d : phase_d1) {
    peak = std::max(peak, d);
    mean += d / 32.0;
  }
  CHECK(peak < 1.2 * mean);  // the stepped gain measured 3.2x
}
