#include <doctest/doctest.h>
#include <algorithm>
#include <cmath>
#include <limits>
#include <memory>
#include <vector>
#include "dsp/modules.h"

using namespace ms::dsp;

namespace {
double dft_mag(const std::vector<float>& x, double f, double r) {
  double re = 0.0, im = 0.0;
  for (size_t n = 0; n < x.size(); ++n) {
    const double a = 2.0 * M_PI * f * static_cast<double>(n) / r;
    re += x[n] * std::cos(a);
    im -= x[n] * std::sin(a);
  }
  return 2.0 / static_cast<double>(x.size()) * std::sqrt(re * re + im * im);
}
ConfigValue cnum(double x) { ConfigValue c; c.is_num = true; c.num = x; return c; }
struct Rig {
  std::unique_ptr<Module> m;
  float ins[5] = {};
  float outs[1] = {};
  explicit Rig(double r, const Config& cfg = {}) {
    const ModuleInfo* i = find_module("phaser");
    REQUIRE(i != nullptr);
    m = i->create();
    m->init(r, cfg);
    ins[1] = 0.3f;
    ins[2] = 0.6f;
    ins[3] = 0.3f;
    ins[4] = 0.5f;
  }
  float tick(float x) { ins[0] = x; m->process(ins, outs); return outs[0]; }
};
}  // namespace

TEST_CASE("phaser: registered surface") {
  const ModuleInfo* i = find_module("phaser");
  REQUIRE(i != nullptr);
  CHECK(i->inputs.size() == 5);
  CHECK(i->outputs.size() == 1);
  CHECK(i->input_index("in") == 0);
  CHECK(i->input_index("feedback") == 3);
  CHECK(i->input_index("mix") == 4);
  CHECK(i->config("stages") != nullptr);
  CHECK(i->config("spread") != nullptr);
}

TEST_CASE("phaser: mix 0 passes dry through") {
  Rig rig(48000.0);
  rig.ins[4] = 0.0f;
  for (int i = 0; i < 2000; ++i) {
    const float x = std::sin(static_cast<float>(i) * 0.05f);
    CHECK(rig.tick(x) == doctest::Approx(x).epsilon(1e-4));
  }
}

TEST_CASE("phaser: produces a spectral notch and stays bounded") {
  Config cfg;
  cfg.push_back({"stages", cnum(6)});
  Rig rig(48000.0, cfg);
  rig.ins[3] = 0.0f;  // no feedback -> clean notch
  const int N = 48000;
  std::vector<float> y(static_cast<size_t>(N));
  float peak = 0.0f;
  for (int i = 0; i < N; ++i) {
    float x = 0.0f;
    for (int k = 1; k <= 20; ++k)
      x += 0.05f * std::sin(2.0f * static_cast<float>(M_PI) * 220.0f *
                            static_cast<float>(k * i) / 48000.0f);
    y[static_cast<size_t>(i)] = rig.tick(x);
    peak = std::max(peak, std::abs(y[static_cast<size_t>(i)]));
  }
  CHECK(std::isfinite(peak));
  CHECK(peak < 8.0f);
  double sum = 0.0, lo = 1e9;
  for (int k = 1; k <= 20; ++k) {
    const double mag = dft_mag(y, 220.0 * k, 48000.0);
    sum += mag;
    lo = std::min(lo, mag);
  }
  CHECK(lo < 0.5 * (sum / 20.0));  // at least one partial notched below the mean
}

TEST_CASE("phaser: feedback stays bounded at 0.95") {
  Rig rig(48000.0);
  rig.ins[3] = 0.95f;
  for (int i = 0; i < 48000; ++i) {
    const float o = rig.tick(std::sin(static_cast<float>(i) * 0.03f));
    CHECK(std::isfinite(o));
    CHECK(std::abs(o) < 8.0f);
  }
}

TEST_CASE("phaser: finite under hostile CV") {
  Rig rig(1000.0);
  const float nan = std::numeric_limits<float>::quiet_NaN();
  rig.ins[1] = nan;
  rig.ins[2] = nan;
  rig.ins[3] = nan;
  rig.ins[4] = nan;
  for (int i = 0; i < 1000; ++i) CHECK(std::isfinite(rig.tick(0.5f)));
}
