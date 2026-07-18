#include <doctest/doctest.h>
#include <algorithm>
#include <cmath>
#include <string>
#include "dsp/modules.h"

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
  CHECK(info->input_index("fb") == 16);
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
