#include <doctest/doctest.h>
#include <algorithm>
#include <cmath>
#include <limits>
#include <string>
#include <vector>
#include "dsl/parser.h"
#include "dsp/modules.h"
#include "engine/engine.h"
#include "graph/graph.h"

using namespace ms;
using namespace ms::dsp;

namespace {
ConfigValue arr(std::initializer_list<double> v) {
  ConfigValue c;
  c.arr.assign(v.begin(), v.end());
  return c;
}
ConfigValue str(const char* s) { ConfigValue c; c.str = s; return c; }

struct FmRig {
  std::unique_ptr<Module> m;
  float ins[17] = {};
  float outs[7] = {};

  explicit FmRig(double rate, const Config& cfg = {}) {
    const ModuleInfo* info = find_module("fm");
    REQUIRE(info != nullptr);
    m = info->create();
    m->init(rate, cfg);
    ins[1] = 250.0f;  // freq: rate/4 at 1 kHz -> phase 0.25 on the window's 1st tick
    ins[3] = 1.0f;    // vel
  }
  void tick(float gate) {
    ins[0] = gate;
    m->process(ins, outs);
  }
  // Envelope reader: run a 4-tick window and read op1 at its FIRST tick, where
  // phase re-aligns to 0.25 (|sin| = 1) every 4 samples. That first tick lands
  // on env progress (4w+1)/samples during a segment — a clean single point.
  float env4(float gate) {
    float first = 0.0f;
    for (int i = 0; i < 4; ++i) {
      tick(gate);
      if (i == 0) first = std::abs(outs[1]);
    }
    return first;
  }
};
}  // namespace

TEST_CASE("fm: registered with the full port surface") {
  const ModuleInfo* info = find_module("fm");
  REQUIRE(info != nullptr);
  CHECK(info->inputs.size() == 17);
  CHECK(info->outputs.size() == 7);
  CHECK(info->input_index("gate") == 0);
  CHECK(info->input_index("freq") == 1);
  CHECK(info->input_index("v_oct") == 2);
  CHECK(info->input_index("vel") == 3);
  CHECK(info->input_index("lvl1") == 4);
  CHECK(info->input_index("lvl6") == 9);
  CHECK(info->input_index("ratio1") == 10);
  CHECK(info->input_index("ratio6") == 15);
  CHECK(info->input_index("fbmod") == 16);
  CHECK(info->output_index("out") == 0);
  CHECK(info->output_index("op6") == 6);
  CHECK(info->config("algo") != nullptr);
  CHECK(info->config("route") != nullptr);
  CHECK(info->config("vel_sens") != nullptr);
}

TEST_CASE("fm: bare default sounds only op1 as a plain gated sine") {
  FmRig rig(1000.0);
  for (int i = 0; i < 600; ++i) rig.tick(1.0f);  // settle to l3 = 0.5
  float peak = 0.0f;
  for (int i = 0; i < 8; ++i) {
    rig.tick(1.0f);
    peak = std::max(peak, std::abs(rig.outs[1]));
    CHECK(rig.outs[0] == doctest::Approx(rig.outs[1]));  // out == op1 (sole carrier)
    for (int k = 2; k <= 6; ++k) CHECK(rig.outs[k] == 0.0f);  // ops 2..6 default level 0
  }
  CHECK(peak == doctest::Approx(0.5).epsilon(0.05));  // held at l3 = 0.5
}

TEST_CASE("fm: free sums carriers at their levels") {
  Config cfg;
  cfg.push_back({"levels", arr({1, .5, .25})});
  FmRig rig(1000.0, cfg);
  for (int i = 0; i < 600; ++i) rig.tick(1.0f);  // settle at l3 = .5
  float peak2 = 0.0f;
  for (int i = 0; i < 8; ++i) {
    rig.tick(1.0f);
    peak2 = std::max(peak2, std::abs(rig.outs[2]));
    CHECK(rig.outs[0] == doctest::Approx(rig.outs[1] + rig.outs[2] + rig.outs[3]).epsilon(1e-5));
    CHECK(rig.outs[4] == 0.0f);
  }
  CHECK(peak2 == doctest::Approx(0.25).epsilon(0.05));  // .5 level × l3 .5 envelope
}

TEST_CASE("fm: envelope walks its four segments on the given times") {
  Config cfg;
  cfg.push_back({"t1", arr({.016})});  // 16 samples at 1 kHz
  cfg.push_back({"t2", arr({.032})});
  cfg.push_back({"t3", arr({.040})});
  cfg.push_back({"t4", arr({.040})});
  cfg.push_back({"l2", arr({.5})});
  cfg.push_back({"l3", arr({.25})});
  FmRig rig(1000.0, cfg);

  // attack: window first ticks land on progress 1/16, 5/16, 9/16, 13/16
  CHECK(rig.env4(1.0f) == doctest::Approx(1.0 / 16).epsilon(.02));
  CHECK(rig.env4(1.0f) == doctest::Approx(5.0 / 16).epsilon(.02));
  CHECK(rig.env4(1.0f) == doctest::Approx(9.0 / 16).epsilon(.02));
  CHECK(rig.env4(1.0f) == doctest::Approx(13.0 / 16).epsilon(.02));
  for (int w = 0; w < 8; ++w) (void)rig.env4(1.0f);  // finish S1+S2 into l2
  CHECK(rig.env4(1.0f) == doctest::Approx(.5).epsilon(.05));       // end of S2 → l2
  for (int w = 0; w < 10; ++w) (void)rig.env4(1.0f);  // finish S3 (40 samples)
  CHECK(rig.env4(1.0f) == doctest::Approx(.25).epsilon(.05));      // end of S3 → l3
  CHECK(rig.env4(1.0f) == doctest::Approx(.25).epsilon(.02));      // Hold
  // release from l3 over 40 samples
  CHECK(rig.env4(0.0f) < .25);
  for (int w = 0; w < 11; ++w) (void)rig.env4(0.0f);
  CHECK(rig.env4(0.0f) == doctest::Approx(0.0).epsilon(.01));
}

TEST_CASE("fm: release and retrigger run from the current level") {
  Config cfg;
  cfg.push_back({"t1", arr({.016})});
  cfg.push_back({"t4", arr({.040})});
  FmRig rig(1000.0, cfg);
  for (int i = 0; i < 8; ++i) rig.tick(1.0f);  // mid-attack, level ~ .5
  const float first = rig.env4(0.0f);
  CHECK(first < 0.65f);  // releases from where it was, not from 1
  CHECK(first > 0.2f);
  for (int i = 0; i < 48; ++i) rig.tick(0.0f);  // idle
  for (int i = 0; i < 8; ++i) rig.tick(1.0f);   // retrigger mid-attack
  for (int i = 0; i < 40; ++i) rig.tick(0.0f);  // release fully
  for (int i = 0; i < 20; ++i) rig.tick(0.0f);
  const float low = rig.env4(0.0f);
  CHECK(low == doctest::Approx(0.0).epsilon(.02));
  const float re = rig.env4(1.0f);
  CHECK(re < 0.35f);  // retrigger climbs from ~0, not from 1
  CHECK(re > 0.0f);
}

TEST_CASE("fm: zero-time segments jump in one sample") {
  Config cfg;
  cfg.push_back({"t1", arr({0.0})});
  FmRig rig(1000.0, cfg);
  CHECK(rig.env4(1.0f) > 0.9f);  // S1 completes on the first tick
}

TEST_CASE("fm: curve shapes the segment") {
  Config cfg;
  cfg.push_back({"curve", str("exp")});
  cfg.push_back({"t1", arr({.016})});
  FmRig rig(1000.0, cfg);
  (void)rig.env4(1.0f);
  CHECK(rig.env4(1.0f) < 0.15f);  // x² at progress 5/16 ≈ .098 (lin would be .3125)

  Config cfg2;
  cfg2.push_back({"curve", str("log")});
  cfg2.push_back({"t1", arr({.016})});
  FmRig rig2(1000.0, cfg2);
  (void)rig2.env4(1.0f);
  CHECK(rig2.env4(1.0f) > 0.5f);  // √x at 5/16 ≈ .56
}

TEST_CASE("fm: ratio scales op frequency") {
  Config cfg;
  cfg.push_back({"levels", arr({1, 1})});
  cfg.push_back({"ratios", arr({1, 2})});
  FmRig rig(1000.0, cfg);
  auto crossings = [&](int tap, int n) {
    int count = 0;
    float prev = 0.0f;
    for (int i = 0; i < n; ++i) {
      rig.tick(1.0f);
      const float s = rig.outs[tap];
      if (prev < 0.0f && s >= 0.0f) ++count;
      prev = s;
    }
    return count;
  };
  const int c1 = crossings(1, 1000);
  // re-sync phases for a clean op2 count
  FmRig rig2(1000.0, cfg);
  const int c2 = [&] {
    int count = 0;
    float prev = 0.0f;
    for (int i = 0; i < 1000; ++i) {
      rig2.tick(1.0f);
      const float s = rig2.outs[2];
      if (prev < 0.0f && s >= 0.0f) ++count;
      prev = s;
    }
    return count;
  }();
  CHECK(c2 == doctest::Approx(2.0 * c1).epsilon(4));
}

namespace {
// One-sided DFT magnitude at an exact bin (coherent tones only).
double dft_mag(const std::vector<float>& x, double freq, double rate) {
  double re = 0.0, im = 0.0;
  for (size_t n = 0; n < x.size(); ++n) {
    const double a = 2.0 * M_PI * freq * static_cast<double>(n) / rate;
    re += x[n] * std::cos(a);
    im -= x[n] * std::sin(a);
  }
  return 2.0 / static_cast<double>(x.size()) * std::sqrt(re * re + im * im);
}

Config two_op_cfg() {
  Config cfg;
  cfg.push_back({"algo", str("custom")});
  cfg.push_back({"route", arr({2, 1})});
  cfg.push_back({"levels", arr({1, .25})});
  cfg.push_back({"ratios", arr({1, .1})});
  cfg.push_back({"t1", arr({0, 0})});
  cfg.push_back({"t2", arr({0, 0})});
  cfg.push_back({"t3", arr({0, 0})});
  cfg.push_back({"t4", arr({.1, .1})});
  cfg.push_back({"l2", arr({1, 1})});
  cfg.push_back({"l3", arr({1, 1})});
  return cfg;
}
}  // namespace

TEST_CASE("fm: sideband magnitudes match Bessel functions of the index") {
  const double rate = 48000.0;
  FmRig rig(rate, two_op_cfg());
  rig.ins[1] = 4800.0f;  // carrier; modulator ratio .1 -> 480 Hz spacing
  const int N = 48000;
  std::vector<float> x(static_cast<size_t>(N));
  for (int i = 0; i < N; ++i) {
    rig.tick(1.0f);
    x[static_cast<size_t>(i)] = rig.outs[0];
  }
  // Index = kIndex * level * env = 4π * .25 * 1 = π.
  for (int k = -3; k <= 3; ++k) {
    CAPTURE(k);
    const double bin = 4800.0 + 480.0 * k;
    const double expect = std::abs(std::cyl_bessel_j(std::abs(k), M_PI));
    CHECK(dft_mag(x, bin, rate) == doctest::Approx(expect).epsilon(0.05));
  }
  // and nothing where there should be nothing
  CHECK(dft_mag(x, 4800.0 + 240.0, rate) < 0.01);
}

TEST_CASE("fm: carriers are the ops with no outgoing edge") {
  Config cfg = two_op_cfg();
  FmRig rig(1000.0, cfg);
  float max_diff = 0.0f;
  for (int i = 0; i < 200; ++i) {
    rig.tick(1.0f);
    CHECK(rig.outs[0] == doctest::Approx(rig.outs[1]).epsilon(1e-5));  // op1 is the sole carrier
    max_diff = std::max(max_diff, std::abs(rig.outs[2] - rig.outs[1]));
  }
  CHECK(max_diff > 0.1f);  // op2 is a distinct modulator waveform, not the carrier
}

TEST_CASE("fm: a carrier at level 0 silences out while its modulator sounds") {
  Config cfg;
  cfg.push_back({"algo", str("custom")});
  cfg.push_back({"route", arr({2, 1})});
  cfg.push_back({"levels", arr({0, 1})});
  FmRig rig(1000.0, cfg);
  float mod_peak = 0.0f;
  for (int i = 0; i < 40; ++i) {
    rig.tick(1.0f);
    CHECK(rig.outs[0] == 0.0f);
    mod_peak = std::max(mod_peak, std::abs(rig.outs[2]));
  }
  CHECK(mod_peak > 0.4f);  // level 1 × l3 .5 × |sin| peak 1, with margin
}

TEST_CASE("fm: malformed route falls back to free") {
  const std::vector<std::vector<double>> routes = {
      {2},           // odd count
      {7, 1},        // out of range
      {2, 2},        // self-edge (belongs in fb)
      {2, 1, 2, 1},  // duplicate
      {2, 1, 1, 2},  // cycle
  };
  for (const auto& route : routes) {
    CAPTURE(route.size());
    Config bad;
    bad.push_back({"algo", str("custom")});
    bad.push_back({"levels", arr({1, 1, 1, 1, 1, 1})});
    ConfigValue r;
    r.arr = route;
    bad.push_back({"route", r});
    Config good;
    good.push_back({"algo", str("free")});
    good.push_back({"levels", arr({1, 1, 1, 1, 1, 1})});
    FmRig a(1000.0, bad), b(1000.0, good);
    for (int i = 0; i < 2000; ++i) {
      a.tick(1.0f);
      b.tick(1.0f);
      for (int o = 0; o < 7; ++o) CHECK(a.outs[o] == b.outs[o]);
    }
  }
}

TEST_CASE("fm: feedback brightens a lone carrier") {
  auto third_harmonic_ratio = [](float fb) {
    Config cfg;
    cfg.push_back({"fb", arr({fb})});
    cfg.push_back({"t1", arr({0.0})});
    cfg.push_back({"t2", arr({0.0})});
    cfg.push_back({"t3", arr({0.0})});
    cfg.push_back({"l2", arr({1.0})});
    cfg.push_back({"l3", arr({1.0})});
    FmRig rig(48000.0, cfg);
    rig.ins[1] = 480.0f;
    const int N = 48000;
    std::vector<float> x(static_cast<size_t>(N));
    for (int i = 0; i < N; ++i) {
      rig.tick(1.0f);
      x[static_cast<size_t>(i)] = rig.outs[0];
    }
    return dft_mag(x, 1440.0, 48000.0) / dft_mag(x, 480.0, 48000.0);
  };
  CHECK(third_harmonic_ratio(0.6f) > 0.02f);
  CHECK(third_harmonic_ratio(0.0f) < 1e-4f);
  CHECK(third_harmonic_ratio(0.6f) > 10.0f * third_harmonic_ratio(0.0f));
}

TEST_CASE("fm: feedback stays bounded at maximum") {
  Config cfg;
  cfg.push_back({"fb", arr({.999})});
  FmRig rig(48000.0, cfg);
  rig.ins[1] = 480.0f;
  float peak = 0.0f;
  for (int i = 0; i < 48000; ++i) {
    rig.tick(1.0f);
    CHECK(std::isfinite(rig.outs[0]));
    peak = std::max(peak, std::abs(rig.outs[0]));
  }
  CHECK(peak <= 1.0001f);
}

TEST_CASE("fm: fb array slot selects the feedback op") {
  Config cfg;
  cfg.push_back({"algo", str("custom")});
  cfg.push_back({"route", arr({2, 1})});
  cfg.push_back({"levels", arr({1, 1})});
  cfg.push_back({"fb", arr({0, .3})});  // op2 self-edge
  cfg.push_back({"t1", arr({0, 0})});
  cfg.push_back({"t2", arr({0, 0})});
  cfg.push_back({"t3", arr({0, 0})});
  cfg.push_back({"l2", arr({1, 1})});
  cfg.push_back({"l3", arr({1, 1})});
  FmRig rig(48000.0, cfg);
  rig.ins[1] = 480.0f;
  const int N = 48000;
  std::vector<float> mod(static_cast<size_t>(N));
  for (int i = 0; i < N; ++i) {
    rig.tick(1.0f);
    mod[static_cast<size_t>(i)] = rig.outs[2];  // op2 tap
  }
  // op2 at ratio 1 (480 Hz): self-feedback must put energy at 3×480.
  CHECK(dft_mag(mod, 1440.0, 48000.0) > 0.01);
}

TEST_CASE("fm: named algorithms equal their custom expansions") {
  const std::pair<const char*, std::vector<double>> kAlgosToTest[] = {
      {"free", {}},
      {"triad", {2, 1, 4, 3, 6, 5}},
      {"dual", {3, 2, 2, 1, 6, 5, 5, 4}},
      {"cascade", {6, 5, 5, 4, 4, 3, 3, 2, 2, 1}},
      {"crown", {2, 1, 3, 1, 4, 1, 5, 1, 6, 1}},
      {"weave", {6, 4, 5, 4, 4, 2, 3, 2, 2, 1}},
  };
  for (const auto& [name, route] : kAlgosToTest) {
    CAPTURE(name);
    Config a, b;
    a.push_back({"algo", str(name)});
    b.push_back({"algo", str("custom")});
    ConfigValue r;
    r.arr = route;
    b.push_back({"route", r});
    for (Config* c : {&a, &b}) {
      c->push_back({"levels", arr({.9, .4, .3, .5, .2, .6})});
      c->push_back({"ratios", arr({1, 2, 3, 1.5, .5, .75})});
      c->push_back({"fb", arr({0, 0, 0, .3, 0, 0})});
    }
    FmRig ra(1000.0, a), rb(1000.0, b);
    for (int i = 0; i < 5000; ++i) {
      const float gate = (i % 1000) < 700 ? 1.0f : 0.0f;  // exercise retrigger
      ra.tick(gate);
      rb.tick(gate);
      for (int o = 0; o < 7; ++o) CHECK(ra.outs[o] == rb.outs[o]);
    }
  }
}

TEST_CASE("fm: vel scales op levels by vel_sens") {
  Config cfg;
  cfg.push_back({"vel_sens", arr({1})});
  FmRig rig(1000.0, cfg);
  float silent = 0.0f;
  for (int i = 0; i < 2000; ++i) {
    rig.ins[3] = 0.0f;  // vel 0, sens 1 -> op1 closed
    rig.tick(1.0f);
    silent = std::max(silent, std::abs(rig.outs[0]));
  }
  CHECK(silent == 0.0f);
  float half = 0.0f;
  for (int i = 0; i < 2000; ++i) {
    rig.ins[3] = 0.5f;
    rig.tick(1.0f);
    half = std::max(half, std::abs(rig.outs[0]));
  }
  CHECK(half == doctest::Approx(0.25).epsilon(0.05));  // .5 vel × l3 .5
}

TEST_CASE("fm: sens 0 leaves vel inert") {
  FmRig rig(1000.0);  // vel_sens defaults all 0
  float peak = 0.0f;
  for (int i = 0; i < 400; ++i) {
    rig.ins[3] = 0.0f;
    rig.tick(1.0f);
    peak = std::max(peak, std::abs(rig.outs[0]));
  }
  CHECK(peak > 0.4f);
}

TEST_CASE("fm: lvl inputs sum onto op levels") {
  Config cfg = two_op_cfg();  // route 2->1, levels [1, .25]
  FmRig rig(48000.0, cfg);
  rig.ins[5] = 0.5f;  // lvl2: .25 + .5 = .75
  double sum2 = 0.0;
  for (int i = 0; i < 4800; ++i) {
    rig.tick(1.0f);
    sum2 += static_cast<double>(rig.outs[2]) * rig.outs[2];
  }
  const double rms = std::sqrt(sum2 / 4800.0);
  CHECK(rms == doctest::Approx(0.75 / std::sqrt(2.0)).epsilon(0.03));
}

TEST_CASE("fm: ratio inputs sum onto ratios") {
  Config cfg;
  cfg.push_back({"levels", arr({1, 1})});
  cfg.push_back({"ratios", arr({1, 1})});
  FmRig rig(1000.0, cfg);
  rig.ins[11] = 1.0f;  // ratio2: 1 + 1 = 2 -> op2 at 2x op1
  int c1 = 0, c2 = 0;
  float p1 = 0.0f, p2 = 0.0f;
  for (int i = 0; i < 1000; ++i) {
    rig.tick(1.0f);
    if (p1 < 0.0f && rig.outs[1] >= 0.0f) ++c1;
    if (p2 < 0.0f && rig.outs[2] >= 0.0f) ++c2;
    p1 = rig.outs[1];
    p2 = rig.outs[2];
  }
  CHECK(c2 == doctest::Approx(2.0 * c1).epsilon(4));
}

TEST_CASE("fm: fb input adds to every op's feedback") {
  auto third = [](float fb_cv) {
    FmRig rig(48000.0);
    rig.ins[1] = 480.0f;
    rig.ins[16] = fb_cv;
    const int N = 48000;
    std::vector<float> x(static_cast<size_t>(N));
    for (int i = 0; i < N; ++i) {
      rig.tick(1.0f);
      x[static_cast<size_t>(i)] = rig.outs[0];
    }
    return dft_mag(x, 1440.0, 48000.0);
  };
  CHECK(third(0.6f) > 10.0f * third(0.0f));
}

TEST_CASE("fm: fixed ops ignore v_oct and ratio") {
  Config cfg;
  cfg.push_back({"algo", str("custom")});
  cfg.push_back({"route", arr({2, 1})});
  cfg.push_back({"levels", arr({1, 1})});
  cfg.push_back({"fixed", arr({0, 55})});
  auto crossings = [&](float voct, int tap) {
    FmRig rig(48000.0, cfg);
    rig.ins[1] = 440.0f;
    rig.ins[2] = voct;
    int count = 0;
    float prev = 0.0f;
    for (int i = 0; i < 48000; ++i) {
      rig.tick(1.0f);
      const float s = rig.outs[tap];
      if (prev < 0.0f && s >= 0.0f) ++count;
      prev = s;
    }
    return count;
  };
  CHECK(crossings(0.0f, 2) == doctest::Approx(55).epsilon(3));
  CHECK(crossings(1.0f, 2) == doctest::Approx(55).epsilon(3));  // pinned
  const double c1lo = crossings(0.0f, 1), c1hi = crossings(1.0f, 1);
  CHECK(c1hi == doctest::Approx(2.0 * c1lo).epsilon(8));  // carrier still tracks
}

TEST_CASE("fm: sync off free-runs phases") {
  auto attacks = [](const char* sync) {
    Config cfg;
    cfg.push_back({"sync", str(sync)});
    cfg.push_back({"t4", arr({.01})});  // release fully completes within the 2400-sample gap,
                                        // so both pulses' envelopes start from idle 0
    FmRig rig(48000.0, cfg);
    rig.ins[1] = 1000.0f;  // not period-aligned with the pulse spacing
    std::array<std::array<float, 32>, 2> w{};
    for (int pulse = 0; pulse < 2; ++pulse) {
      for (int i = 0; i < 2400; ++i) rig.tick(0.0f);
      for (int i = 0; i < 32; ++i) {
        rig.tick(1.0f);
        w[static_cast<size_t>(pulse)][static_cast<size_t>(i)] = rig.outs[1];
      }
      for (int i = 0; i < 32; ++i) rig.tick(1.0f);
    }
    return w;
  };
  const auto on = attacks("on");
  const auto off = attacks("off");
  for (int i = 0; i < 32; ++i) CHECK(on[0][static_cast<size_t>(i)] == doctest::Approx(on[1][static_cast<size_t>(i)]).epsilon(1e-6));
  bool differ = false;
  for (int i = 0; i < 32; ++i)
    if (std::abs(off[0][static_cast<size_t>(i)] - off[1][static_cast<size_t>(i)]) > 1e-6f)
      differ = true;
  CHECK(differ);
}

TEST_CASE("fm: state migration continues identically") {
  Config cfg;
  cfg.push_back({"algo", str("dual")});
  cfg.push_back({"levels", arr({.9, .4, .3, .5, .2, .6})});
  cfg.push_back({"ratios", arr({1, 2, 3, 1.5, .5, .75})});
  cfg.push_back({"fb", arr({0, 0, 0, .4, 0, 0})});
  FmRig a(1000.0, cfg), b(1000.0, cfg);
  for (int i = 0; i < 3000; ++i) a.tick((i % 500) < 400 ? 1.0f : 0.0f);
  b.m->copy_state_from(*a.m);
  for (int i = 0; i < 2000; ++i) {
    const float g = (i % 500) < 400 ? 1.0f : 0.0f;
    a.tick(g);
    b.tick(g);
    for (int o = 0; o < 7; ++o) CHECK(a.outs[o] == b.outs[o]);
  }
}

TEST_CASE("fm: hostile inputs stay finite") {
  FmRig rig(1000.0);
  const float nan = std::numeric_limits<float>::quiet_NaN();
  rig.ins[1] = nan;   // freq
  rig.ins[2] = nan;   // v_oct
  rig.ins[3] = nan;   // vel
  rig.ins[4] = nan;   // lvl1
  rig.ins[10] = nan;  // ratio1
  rig.ins[16] = nan;  // fb
  for (int i = 0; i < 1000; ++i) {
    rig.tick(1.0f);
    for (float o : rig.outs) CHECK(std::isfinite(o));
  }
}

TEST_CASE("fm: voice-group patch renders non-silent and bounded") {
  const char* src = R"(
clk = clock { bpm: 240 }
wander = random { rate: 2.7, smooth: 0, seed: 21 }
notes = sh { mode: sample }
wander -> notes.in
clk -> notes.trig
key = quant { scale: [0, 3, 5, 7, 10] }
notes -> key.in
poly = alloc { n: 3 }
clk -> poly.gate
key -> poly.voct
ep = voice { notes: poly }
ep.fm = fm { algo: dual, ratios: [1, 1, 7, 1, 14, 1], levels: [.5, .3, .1, .45, .1, .15] }
poly.voct -> ep.fm.v_oct
poly.gate -> ep.fm.gate
ep.fm -> out.in : 0.3
)";
  auto p = dsl::parse(src);
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
  CHECK(std::sqrt(sum2 / static_cast<double>(n)) > 0.01);
  CHECK(peak <= 1.0f);
}
