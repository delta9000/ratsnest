#include <doctest/doctest.h>
#include <cmath>
#include "dsp/modules.h"

using namespace ms::dsp;

namespace {
std::unique_ptr<Module> make_reverb(double rate = 48000.0, Config cfg = {}) {
  const ModuleInfo* info = find_module("reverb");
  REQUIRE(info != nullptr);
  auto m = info->create();
  m->init(rate, cfg);
  return m;
}

// in_l, in_r, decay, damp, mix, freeze
struct Rig {
  std::unique_ptr<Module> m = make_reverb();
  float ins[6] = {0.0f, 0.0f, 0.5f, 0.25f, 1.0f, 0.0f};  // wet-only default
  float out[2] = {0.0f, 0.0f};
  void tick(float l, float r) {
    ins[0] = l;
    ins[1] = r;
    m->process(ins, out);
  }
  double window_rms(int samples) {  // advances time, input silent
    double sum = 0.0;
    for (int i = 0; i < samples; ++i) {
      tick(0.0f, 0.0f);
      sum += 0.5 * (static_cast<double>(out[0]) * out[0] +
                    static_cast<double>(out[1]) * out[1]);
    }
    return std::sqrt(sum / samples);
  }
};
}  // namespace

TEST_CASE("reverb registers stereo ports and configs") {
  const ModuleInfo* info = find_module("reverb");
  REQUIRE(info != nullptr);
  CHECK(info->input_index("in_l") == 0);
  CHECK(info->input_index("freeze") == 5);
  CHECK(info->output_index("out_l") == 0);
  CHECK(info->output_index("out_r") == 1);
  CHECK(info->config("predelay") != nullptr);
  CHECK(info->config("mod_depth") != nullptr);
}

TEST_CASE("an impulse grows a tail that decays") {
  Rig r;
  r.tick(1.0f, 1.0f);
  const double early = r.window_rms(24000);   // 0..0.5 s
  const double mid = r.window_rms(24000);     // 0.5..1.0 s
  const double late = r.window_rms(96000);    // 1..3 s
  CHECK(early > 1e-4);   // a tail exists
  CHECK(mid < early);    // and it decays
  CHECK(late < mid);
}

TEST_CASE("decay input controls tail length") {
  Rig shortr;
  Rig longr;
  shortr.ins[2] = 0.2f;
  longr.ins[2] = 0.9f;
  shortr.tick(1.0f, 1.0f);
  longr.tick(1.0f, 1.0f);
  (void)shortr.window_rms(48000);  // skip 1 s
  (void)longr.window_rms(48000);
  const double s = shortr.window_rms(48000);
  const double l = longr.window_rms(48000);
  CHECK(l > s * 5.0);
}

TEST_CASE("stereo outputs are decorrelated, not copies") {
  Rig r;
  r.tick(1.0f, 1.0f);
  double dot = 0.0, ll = 0.0, rr = 0.0;
  for (int i = 0; i < 48000; ++i) {
    r.tick(0.0f, 0.0f);
    dot += static_cast<double>(r.out[0]) * r.out[1];
    ll += static_cast<double>(r.out[0]) * r.out[0];
    rr += static_cast<double>(r.out[1]) * r.out[1];
  }
  REQUIRE(ll > 0.0);
  REQUIRE(rr > 0.0);
  const double corr = dot / std::sqrt(ll * rr);
  CHECK(std::abs(corr) < 0.9);  // 14 distinct taps: strongly decorrelated
}

TEST_CASE("mix zero is a pure dry pass") {
  Rig r;
  r.ins[4] = 0.0f;
  for (int i = 0; i < 100; ++i) {
    const float l = std::sin(0.03f * static_cast<float>(i));
    const float rr = std::cos(0.05f * static_cast<float>(i));
    r.tick(l, rr);
    CHECK(r.out[0] == doctest::Approx(l));
    CHECK(r.out[1] == doctest::Approx(rr));
  }
}

TEST_CASE("damping darkens the tail") {
  Rig bright;
  Rig dark;
  bright.ins[3] = 0.0f;
  dark.ins[3] = 0.95f;
  bright.ins[2] = dark.ins[2] = 0.85f;
  bright.tick(1.0f, 1.0f);
  dark.tick(1.0f, 1.0f);
  // High-frequency proxy: mean |first difference| over the 1..2 s window.
  auto hf = [](Rig& r) {
    (void)r.window_rms(48000);
    double d = 0.0;
    float prev = r.out[0];
    for (int i = 0; i < 48000; ++i) {
      r.tick(0.0f, 0.0f);
      d += std::abs(r.out[0] - prev);
      prev = r.out[0];
    }
    return d;
  };
  const double hb = hf(bright);
  const double hd = hf(dark);
  CHECK(hd < hb * 0.5);
}

TEST_CASE("freeze holds the tank and unfreeze releases it") {
  Rig r;
  r.ins[2] = 0.6f;
  for (int i = 0; i < 24000; ++i) r.tick(i % 7 == 0 ? 0.8f : -0.3f, 0.4f);
  r.ins[5] = 1.0f;  // freeze
  (void)r.window_rms(4800);  // let the 10 ms blend settle, then measure
  const double held_early = r.window_rms(48000);
  const double held_late = r.window_rms(96000);  // 2 s later
  CHECK(held_early > 1e-4);
  CHECK(held_late > held_early * 0.5);  // held, not decaying away
  r.ins[5] = 0.0f;  // unfreeze: decay 0.6 drains the tank
  (void)r.window_rms(96000);
  const double drained = r.window_rms(48000);
  CHECK(drained < held_late * 0.05);
}

TEST_CASE("reverb stays finite under hostile inputs and odd rates") {
  Rig r;
  for (int i = 0; i < 200000; ++i) {
    r.ins[2] = i % 3 == 0 ? 5.0f : -2.0f;    // decay clamp exercised
    r.ins[3] = i % 5 == 0 ? -4.0f : 4.0f;
    r.ins[5] = i % 11 == 0 ? 1.0f : 0.0f;    // freeze flapping
    r.tick(i % 2 == 0 ? 1.0f : -1.0f, i % 3 == 0 ? -1.0f : 1.0f);
    REQUIRE(std::isfinite(r.out[0]));
    REQUIRE(std::isfinite(r.out[1]));
    REQUIRE(std::abs(r.out[0]) < 100.0f);
  }
  // init at a non-reference rate must scale cleanly
  auto hi = make_reverb(96000.0);
  float ins[6] = {1.0f, 1.0f, 0.7f, 0.2f, 1.0f, 0.0f};
  float out[2];
  for (int i = 0; i < 48000; ++i) {
    hi->process(ins, out);
    REQUIRE(std::isfinite(out[0]));
    ins[0] = ins[1] = 0.0f;
  }
}
