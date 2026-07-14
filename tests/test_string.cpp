#include <doctest/doctest.h>
#include <cmath>
#include <limits>
#include <utility>
#include <vector>
#include "dsp/modules.h"

using namespace ms::dsp;

namespace {
Config seed_cfg(double seed) {
  Config cfg;
  ConfigValue v;
  v.num = seed;
  v.is_num = true;
  cfg.emplace_back("seed", v);
  return cfg;
}

// in, freq, v_oct, strike, structure, brightness, damping, position
struct StringRig {
  std::unique_ptr<Module> module;
  float ins[8] = {0.f, 220.f, 0.f, 0.f, 0.25f, 0.7f, 0.6f, 0.25f};
  float outs[2] = {};

  explicit StringRig(double seed = 1.0) {
    const ModuleInfo* info = find_module("string");
    REQUIRE(info != nullptr);
    REQUIRE(info->output_index("aux") == 1);
    module = info->create();
    module->init(48000.0, seed_cfg(seed));
  }

  float tick() {
    module->process(ins, outs);
    return outs[0];
  }

  std::vector<float> pluck_and_collect(int n) {
    ins[3] = 1.0f;
    std::vector<float> out(static_cast<std::size_t>(n));
    out[0] = tick();
    ins[3] = 0.0f;
    for (int i = 1; i < n; ++i) out[static_cast<std::size_t>(i)] = tick();
    return out;
  }
};

double rms(const std::vector<float>& x, std::size_t from, std::size_t to) {
  double s = 0.0;
  for (std::size_t i = from; i < to; ++i)
    s += static_cast<double>(x[i]) * x[i];
  return std::sqrt(s / static_cast<double>(to - from));
}

// Lag of the strongest normalized autocorrelation peak in [lo, hi].
int autocorr_peak(const std::vector<float>& x, int lo, int hi) {
  int best = lo;
  double best_v = -1.0;
  for (int lag = lo; lag <= hi; ++lag) {
    double s = 0.0;
    for (std::size_t i = static_cast<std::size_t>(lag); i < x.size(); ++i)
      s += static_cast<double>(x[i]) * x[i - static_cast<std::size_t>(lag)];
    if (s > best_v) {
      best_v = s;
      best = lag;
    }
  }
  return best;
}
}  // namespace

TEST_CASE("string is exactly silent without excitation") {
  StringRig s;
  for (int i = 0; i < 4800; ++i) CHECK(s.tick() == 0.0f);
}

TEST_CASE("string pluck rings at the requested pitch") {
  StringRig s;
  auto x = s.pluck_and_collect(10192);
  x.erase(x.begin(), x.begin() + 2000);  // drop the attack transient
  // 48000 / 220 = 218.2 samples.
  const int lag = autocorr_peak(x, 150, 300);
  CHECK(lag >= 216);
  CHECK(lag <= 221);
}

TEST_CASE("string v_oct halves the period") {
  StringRig s;
  s.ins[2] = 1.0f;
  auto x = s.pluck_and_collect(10192);
  x.erase(x.begin(), x.begin() + 2000);
  const int lag = autocorr_peak(x, 80, 160);
  CHECK(lag >= 107);
  CHECK(lag <= 111);
}

TEST_CASE("string damping orders decay time; damping 1 sustains") {
  auto tail = [](float damping) {
    StringRig s;
    s.ins[6] = damping;
    const auto x = s.pluck_and_collect(96000);
    return std::pair{rms(x, 24000, 48000), rms(x, 72000, 96000)};
  };
  const auto [mid_lo, late_lo] = tail(0.3f);
  const auto [mid_hi, late_hi] = tail(0.8f);
  CHECK(mid_hi > 2.0 * mid_lo);
  const auto [mid_inf, late_inf] = tail(1.0f);
  CHECK(late_inf > 0.8 * mid_inf);  // within ~1 dB: infinite sustain
}

TEST_CASE("string is seed-deterministic") {
  StringRig a(7.0), b(7.0), c(8.0);
  const auto xa = a.pluck_and_collect(4096);
  const auto xb = b.pluck_and_collect(4096);
  const auto xc = c.pluck_and_collect(4096);
  CHECK(xa == xb);
  CHECK(xa != xc);
}

TEST_CASE("string structure dead zone is pure Karplus-Strong") {
  StringRig a(3.0), b(3.0), c(3.0);
  a.ins[4] = 0.245f;
  b.ins[4] = 0.255f;
  c.ins[4] = 0.6f;
  const auto xa = a.pluck_and_collect(8192);
  const auto xb = b.pluck_and_collect(8192);
  const auto xc = c.pluck_and_collect(8192);
  CHECK(xa == xb);   // both in the 0.24..0.26 dead zone
  CHECK(xa != xc);   // dispersion audibly engaged outside it
}

TEST_CASE("string survives hostile inputs and recovers") {
  StringRig s;
  s.ins[3] = 1.0f;
  s.tick();
  s.ins[3] = 0.0f;
  s.ins[0] = std::numeric_limits<float>::quiet_NaN();
  s.ins[1] = -std::numeric_limits<float>::infinity();
  s.ins[7] = 1e18f;
  for (int i = 0; i < 512; ++i) s.tick();
  s.ins[0] = 0.0f;
  s.ins[1] = 220.0f;
  s.ins[7] = 0.25f;
  bool finite = true;
  float last = 0.0f;
  for (int i = 0; i < 48000; ++i) last = s.tick();
  for (int i = 0; i < 256; ++i) finite = finite && std::isfinite(s.tick());
  CHECK(finite);
  (void)last;
}

TEST_CASE("string state migrates across instances") {
  StringRig a(5.0), b(5.0);
  a.ins[3] = 1.0f;
  a.tick();
  a.ins[3] = 0.0f;
  for (int i = 0; i < 3000; ++i) a.tick();
  b.module->copy_state_from(*a.module);
  for (int i = 0; i < 256; ++i) {
    const float ya = a.tick();
    const float yb = b.tick();
    CHECK(ya == doctest::Approx(yb).epsilon(1e-4));
  }
}
