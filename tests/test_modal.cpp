#include <doctest/doctest.h>
#include <cmath>
#include <limits>
#include <numbers>
#include <utility>
#include <vector>
#include "dsp/modules.h"

using namespace ms::dsp;

namespace {
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
