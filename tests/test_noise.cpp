#include <doctest/doctest.h>
#include <cmath>
#include "dsp/modules.h"

using namespace ms::dsp;

namespace {
std::unique_ptr<Module> make_noise(const char* color, double seed = 1.0) {
  const ModuleInfo* info = find_module("noise");
  REQUIRE(info != nullptr);
  auto m = info->create();
  Config cfg;
  ConfigValue c; c.str = color;
  cfg.push_back({"color", c});
  ConfigValue s; s.num = seed; s.is_num = true;
  cfg.push_back({"seed", s});
  m->init(48000.0, cfg);
  return m;
}
}  // namespace

TEST_CASE("noise is bounded, nonzero, and deterministic per seed") {
  auto a = make_noise("white", 7.0);
  auto b = make_noise("white", 7.0);
  auto c = make_noise("white", 8.0);
  float oa[1], ob[1], oc[1];
  bool differs_from_other_seed = false;
  double sumsq = 0.0;
  for (int i = 0; i < 4096; ++i) {
    a->process(nullptr, oa);
    b->process(nullptr, ob);
    c->process(nullptr, oc);
    CHECK(std::abs(oa[0]) <= 1.0f);
    CHECK(oa[0] == ob[0]);  // same seed, same stream
    if (oa[0] != oc[0]) differs_from_other_seed = true;
    sumsq += static_cast<double>(oa[0]) * oa[0];
  }
  CHECK(differs_from_other_seed);
  CHECK(std::sqrt(sumsq / 4096.0) > 0.1);  // actually noisy, not near-silent
}

TEST_CASE("pink noise is smoother than white noise") {
  auto white = make_noise("white");
  auto pink = make_noise("pink");
  float ow[1], op[1];
  float prev_w = 0.0f, prev_p = 0.0f;
  double dw = 0.0, dp = 0.0;
  for (int i = 0; i < 8192; ++i) {
    white->process(nullptr, ow);
    pink->process(nullptr, op);
    if (i > 0) {
      dw += std::abs(ow[0] - prev_w);
      dp += std::abs(op[0] - prev_p);
    }
    prev_w = ow[0];
    prev_p = op[0];
    CHECK(std::abs(op[0]) <= 1.5f);
  }
  CHECK(dp < dw * 0.5);  // pink: less high-frequency energy
}
