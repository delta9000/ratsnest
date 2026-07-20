#include <doctest/doctest.h>
#include <cmath>
#include <limits>
#include <numbers>
#include <utility>
#include <vector>
#include "dsp/modules.h"

using namespace ms::dsp;

namespace {
Config ratios_cfg(std::initializer_list<double> rs) {
  ConfigValue v;
  v.arr.assign(rs.begin(), rs.end());
  Config c;
  c.push_back({"ratios", std::move(v)});
  return c;
}

// in, freq, v_oct, strike, structure, brightness, damping, position
struct ModalRig {
  std::unique_ptr<Module> module;
  float ins[8] = {0.f, 220.f, 0.f, 0.f, 0.25f, 0.5f, 0.5f, 0.25f};
  float outs[3] = {};

  explicit ModalRig(const Config& cfg = {}) {
    const ModuleInfo* info = find_module("modal");
    REQUIRE(info != nullptr);
    REQUIRE(info->input_index("strike") == 3);
    REQUIRE(info->output_index("even") == 2);
    module = info->create();
    module->init(48000.0, cfg);
  }

  float tick() {
    module->process(ins, outs);
    return outs[0];
  }

  // Strike once, then collect n samples of free decay.
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

double goertzel(const std::vector<float>& x, double hz) {
  const double w = 2.0 * std::numbers::pi * hz / 48000.0;
  const double c = 2.0 * std::cos(w);
  double s1 = 0.0, s2 = 0.0;
  for (float v : x) {
    const double s0 = static_cast<double>(v) + c * s1 - s2;
    s2 = s1;
    s1 = s0;
  }
  return std::sqrt(std::max(s1 * s1 + s2 * s2 - c * s1 * s2, 0.0));
}
}  // namespace

TEST_CASE("modal is exactly silent without excitation") {
  ModalRig m;
  for (int i = 0; i < 4800; ++i) CHECK(m.tick() == 0.0f);
}

TEST_CASE("modal strike rings and decays") {
  ModalRig m;
  const auto x = m.pluck_and_collect(48000);
  CHECK(rms(x, 0, 4800) > 1e-3);
  CHECK(rms(x, 43200, 48000) < rms(x, 0, 4800));
}

TEST_CASE("modal damping extends decay time") {
  auto tail = [](float damping) {
    ModalRig m;
    m.ins[6] = damping;
    const auto x = m.pluck_and_collect(48000);
    return rms(x, 24000, 48000);
  };
  CHECK(tail(0.75f) > 2.0 * tail(0.25f));
}

TEST_CASE("modal rings at its harmonic partials on the plateau") {
  ModalRig m;  // structure 0.25: stiffness 0, harmonic series
  const auto x = m.pluck_and_collect(16384);
  // Fundamental and 2nd partial well above an off-mode frequency.
  CHECK(goertzel(x, 220.0) > 5.0 * goertzel(x, 311.0));
  CHECK(goertzel(x, 440.0) > 5.0 * goertzel(x, 311.0));
}

TEST_CASE("modal v_oct doubles the fundamental") {
  ModalRig m;
  m.ins[2] = 1.0f;
  const auto x = m.pluck_and_collect(16384);
  CHECK(goertzel(x, 440.0) > 5.0 * goertzel(x, 220.0));
}

TEST_CASE("modal position 0.5 nulls the 2nd partial but not the 1st") {
  auto partial2 = [](float pos) {
    ModalRig m;
    m.ins[7] = pos;
    const auto x = m.pluck_and_collect(16384);
    return std::pair{goertzel(x, 220.0), goertzel(x, 440.0)};
  };
  const auto [f1_half, f2_half] = partial2(0.5f);
  const auto [f1_q, f2_q] = partial2(0.25f);
  CHECK(f2_half < 0.05 * f2_q);       // 2nd partial nulled at center strike
  CHECK(f1_half > 0.25 * f1_q);       // fundamental survives
}

TEST_CASE("modal out is the sum of odd and even outputs") {
  ModalRig m;
  m.ins[3] = 1.0f;
  for (int i = 0; i < 1000; ++i) {
    m.module->process(m.ins, m.outs);
    m.ins[3] = 0.0f;
    CHECK(m.outs[0] ==
          doctest::Approx(m.outs[1] + m.outs[2]).epsilon(1e-4));
  }
}

TEST_CASE("modal survives hostile inputs and recovers") {
  ModalRig m;
  m.ins[3] = 1.0f;
  m.tick();
  m.ins[3] = 0.0f;
  m.ins[0] = std::numeric_limits<float>::quiet_NaN();
  m.ins[1] = std::numeric_limits<float>::infinity();
  m.ins[4] = -1e9f;
  for (int i = 0; i < 256; ++i) m.tick();
  m.ins[0] = 0.0f;
  m.ins[1] = 220.0f;
  m.ins[4] = 0.25f;
  float last = 0.0f;
  for (int i = 0; i < 4800; ++i) last = m.tick();
  CHECK(std::isfinite(last));
}

TEST_CASE("modal state migrates across instances") {
  ModalRig a, b;
  a.ins[3] = 1.0f;
  a.tick();
  a.ins[3] = 0.0f;
  for (int i = 0; i < 2000; ++i) a.tick();
  b.module->copy_state_from(*a.module);
  for (int i = 0; i < 256; ++i) {
    const float ya = a.tick();
    const float yb = b.tick();
    CHECK(ya == doctest::Approx(yb).epsilon(1e-4));
  }
}

TEST_CASE("modal modes config rounds to even and caps the bank") {
  Config cfg;
  ConfigValue v;
  v.num = 7.0;
  v.is_num = true;
  cfg.emplace_back("modes", v);
  ModalRig m(cfg);  // 7 -> 8 modes; just verify it runs and rings
  const auto x = m.pluck_and_collect(4800);
  CHECK(rms(x, 0, 4800) > 1e-4);
}

TEST_CASE("modal migration across a modes-config change keeps ringing") {
  // Spec §6: changing `modes:` migrates the overlapping prefix of the bank.
  ModalRig a;  // 64 modes
  Config cfg;
  ConfigValue v;
  v.num = 32.0;
  v.is_num = true;
  cfg.emplace_back("modes", v);
  ModalRig b(cfg);
  a.ins[3] = 1.0f;
  a.tick();
  a.ins[3] = 0.0f;
  for (int i = 0; i < 2000; ++i) a.tick();
  b.module->copy_state_from(*a.module);
  double sum = 0.0;
  for (int i = 0; i < 4800; ++i) {
    const float y = b.tick();
    CHECK(std::isfinite(y));
    sum += static_cast<double>(y) * y;
  }
  CHECK(std::sqrt(sum / 4800.0) > 1e-5);  // the tail carried over
}

TEST_CASE("modal ratios places modes at explicit multiples of f0") {
  ModalRig r(ratios_cfg({1.0, 1.594, 2.136}));
  r.ins[6] = 0.8f;  // long ring for narrow spectral peaks
  r.ins[7] = 0.1f;  // off the comb null: position 0.25 zeroes mode index 2
  auto x = r.pluck_and_collect(48000);
  // Listed ratio rings; the harmonic-walk position (2*f0) does not.
  CHECK(goertzel(x, 220.0 * 1.594) > 10.0 * goertzel(x, 220.0 * 1.8));
  CHECK(goertzel(x, 220.0 * 2.136) > 10.0 * goertzel(x, 440.0));
}

TEST_CASE("modal structure warps listed ratios, neutral at 0.25") {
  // w = 2^((s-0.25)*2): s = 0.25 -> exact; s = 0.9 -> w ~ 2.462,
  // 1.594^2.462 * 220 ~ 693.5 Hz.
  ModalRig a(ratios_cfg({1.0, 1.594}));
  a.ins[6] = 0.8f;
  auto xa = a.pluck_and_collect(48000);
  CHECK(goertzel(xa, 220.0 * 1.594) > 10.0 * goertzel(xa, 693.5));

  ModalRig b(ratios_cfg({1.0, 1.594}));
  b.ins[6] = 0.8f;
  b.ins[4] = 0.9f;
  auto xb = b.pluck_and_collect(48000);
  CHECK(goertzel(xb, 693.5) > 10.0 * goertzel(xb, 220.0 * 1.594));
}

TEST_CASE("modal ratios entries above 64 clamp and stay finite") {
  // ratio 100.0 clamps to 64.0: mode 1 rings at freq * 64 = 240 * 64 =
  // 15360 Hz. If the clamp were removed, ratio 100 * freq 240 = pf 0.5 ->
  // parked at the 0.49 ceiling instead (23520 Hz), so it would NOT ring at
  // 15360 Hz and this peak would collapse into the noise floor.
  ModalRig r(ratios_cfg({1.0, 100.0}));
  r.ins[1] = 240.0f;
  r.ins[5] = 1.0f;  // brightness: widen the exciter so the high mode gets hit
  r.ins[6] = 0.8f;  // long ring for a narrow spectral peak
  r.ins[7] = 0.1f;  // off the comb null
  auto x = r.pluck_and_collect(48000);
  for (float v : x) CHECK(std::isfinite(v));
  CHECK(goertzel(x, 15360.0) > 10.0 * goertzel(x, 10000.0));
}

TEST_CASE("modal odd ratio count pads without ringing at the pad") {
  ModalRig r(ratios_cfg({1.0, 1.594, 2.136}));  // 3 entries -> bank of 4
  r.ins[7] = 0.1f;  // off the comb null: position 0.25 zeroes mode index 2
  auto x = r.pluck_and_collect(24000);
  CHECK(goertzel(x, 220.0) > 0.0);
  for (float v : x) CHECK(std::isfinite(v));
  // The padded 4th mode is parked at ~23520 Hz (0.49 normalized @ 48 kHz)
  // and should contribute essentially nothing next to the fundamental.
  CHECK(goertzel(x, 23520.0) < 0.1 * goertzel(x, 220.0));
}

TEST_CASE("modal init clamps non-positive ratios defensively (no NaN)") {
  // validate_cfg rejects these at compile time, but init() is handed raw
  // configs directly here. structure != 0.25 makes the mode warp exponent
  // fractional, so a non-positive ratio base to pow() would be NaN.
  ModalRig r(ratios_cfg({1.0, -2.0, 0.0}));
  r.ins[4] = 0.9f;  // structure -> fractional warp exponent
  r.ins[6] = 0.8f;
  auto x = r.pluck_and_collect(4800);
  for (float v : x) CHECK(std::isfinite(v));
}

TEST_CASE("modal migration across a ratios change keeps ringing") {
  ModalRig a(ratios_cfg({1.0, 1.594, 2.136, 2.296}));
  a.pluck_and_collect(2000);
  ModalRig b(ratios_cfg({1.0, 1.594}));
  b.module->copy_state_from(*a.module);
  std::vector<float> tail(4800);
  for (auto& v : tail) v = b.tick();
  CHECK(rms(tail, 0, tail.size()) > 0.0);
  for (float v : tail) CHECK(std::isfinite(v));
}
