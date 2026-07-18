#include <doctest/doctest.h>
#include <algorithm>
#include <cmath>
#include <limits>
#include <memory>
#include "dsp/modules.h"

using namespace ms::dsp;

namespace {
struct Rig {
  std::unique_ptr<Module> m;
  float ins[5] = {};
  float outs[2] = {};
  explicit Rig(double r, const Config& cfg = {}) {
    const ModuleInfo* i = find_module("chorus");
    REQUIRE(i != nullptr);
    m = i->create();
    m->init(r, cfg);
    ins[2] = 0.5f;
    ins[3] = 0.5f;
    ins[4] = 0.5f;
  }
  void tick(float l, float r) {
    ins[0] = l;
    ins[1] = r;
    m->process(ins, outs);
  }
};
}  // namespace

TEST_CASE("chorus: registered stereo surface") {
  const ModuleInfo* i = find_module("chorus");
  REQUIRE(i != nullptr);
  CHECK(i->inputs.size() == 5);
  CHECK(i->outputs.size() == 2);
  CHECK(i->input_index("in_l") == 0);
  CHECK(i->input_index("in_r") == 1);
  CHECK(i->input_index("mix") == 4);
  CHECK(i->output_index("out_l") == 0);
  CHECK(i->output_index("out_r") == 1);
  CHECK(i->config("voices") != nullptr);
  CHECK(i->config("delay") != nullptr);
}

TEST_CASE("chorus: mix 0 passes dry through both channels") {
  Rig rig(48000.0);
  rig.ins[4] = 0.0f;
  for (int i = 0; i < 2000; ++i) {
    const float x = std::sin(static_cast<float>(i) * 0.1f);
    rig.tick(x, -x);
    CHECK(rig.outs[0] == doctest::Approx(x).epsilon(1e-4));
    CHECK(rig.outs[1] == doctest::Approx(-x).epsilon(1e-4));
  }
}

TEST_CASE("chorus: wet modulates a held input over time, L != R") {
  Rig rig(48000.0);
  rig.ins[4] = 1.0f;  // full wet
  float mn = 1e9f, mx = -1e9f;
  bool differ = false;
  for (int i = 0; i < 48000; ++i) {
    rig.tick(1.0f, 1.0f);  // DC in -> wet is time-varying (pitch modulation)
    mn = std::min(mn, rig.outs[0]);
    mx = std::max(mx, rig.outs[0]);
    if (std::abs(rig.outs[0] - rig.outs[1]) > 1e-4f) differ = true;
  }
  CHECK(mx - mn > 0.05f);  // the chorus actually modulates
  CHECK(differ);           // stereo decorrelation
}

TEST_CASE("chorus: finite under hostile CV") {
  Rig rig(1000.0);
  const float nan = std::numeric_limits<float>::quiet_NaN();
  rig.ins[2] = nan;
  rig.ins[3] = nan;
  rig.ins[4] = nan;
  for (int i = 0; i < 1000; ++i) {
    rig.tick(0.5f, 0.5f);
    CHECK(std::isfinite(rig.outs[0]));
    CHECK(std::isfinite(rig.outs[1]));
  }
}
