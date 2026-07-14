#include <doctest/doctest.h>
#include <cmath>
#include <numbers>
#include <string>
#include "dsp/modules.h"

using namespace ms::dsp;

namespace {
std::unique_ptr<Module> make_follow(double rate = 48000.0,
                                    const char* mode = "peak") {
  const ModuleInfo* info = find_module("follow");
  REQUIRE(info != nullptr);
  auto m = info->create();
  Config cfg;
  if (mode) cfg.push_back({"mode", ConfigValue{std::string(mode), 0.0, {}, false}});
  m->init(rate, cfg);
  return m;
}
}  // namespace

TEST_CASE("follow: metadata and defaults") {
  const ModuleInfo* info = find_module("follow");
  REQUIRE(info != nullptr);
  CHECK(std::string(info->type) == "follow");

  REQUIRE(info->inputs.size() == 4);
  CHECK(std::string(info->inputs[0].name) == "in");      CHECK(info->inputs[0].def == 0.0f);
  CHECK(std::string(info->inputs[1].name) == "attack");  CHECK(info->inputs[1].def == 0.01f);
  CHECK(std::string(info->inputs[2].name) == "release"); CHECK(info->inputs[2].def == 0.1f);
  CHECK(std::string(info->inputs[3].name) == "thresh");  CHECK(info->inputs[3].def == 0.1f);

  REQUIRE(info->configs.size() == 1);
  CHECK(std::string(info->configs[0].name) == "mode");

  REQUIRE(info->outputs.size() == 2);
  CHECK(std::string(info->outputs[0].name) == "env");
  CHECK(std::string(info->outputs[1].name) == "gate");
}

TEST_CASE("follow: peak envelope converges to DC magnitude") {
  auto m = make_follow(48000.0, "peak");
  float ins[4] = {0.5f, 0.001f, 0.001f, 0.1f};  // fast ballistics
  float outs[2] = {0.0f, 0.0f};
  for (int i = 0; i < 5000; ++i) m->process(ins, outs);
  CHECK(outs[0] == doctest::Approx(0.5f).epsilon(0.01));
}

TEST_CASE("follow: peak envelope reaches full scale on constant input") {
  auto m = make_follow(48000.0, "peak");
  float ins[4] = {1.0f, 0.001f, 0.1f, 0.1f};
  float outs[2] = {0.0f, 0.0f};
  for (int i = 0; i < 5000; ++i) m->process(ins, outs);
  CHECK(outs[0] > 0.99f);
  CHECK(outs[0] <= 1.0001f);
}

TEST_CASE("follow: peak vs rms detection of a full-scale sine") {
  // Slow symmetric ballistics converge to the analytic averages:
  //   peak (mean |sin|) = 2/pi ~= 0.637   rms (sqrt mean sin^2) = 1/sqrt2 ~= 0.707
  const double step = 2.0 * std::numbers::pi * 1000.0 / 48000.0;  // 1 kHz
  for (int mode = 0; mode < 2; ++mode) {
    auto m = make_follow(48000.0, mode == 0 ? "peak" : "rms");
    float ins[4] = {0.0f, 0.05f, 0.05f, 0.1f};
    float outs[2] = {0.0f, 0.0f};
    double ph = 0.0, acc = 0.0;
    int n = 0;
    for (int i = 0; i < 20000; ++i) {
      ins[0] = static_cast<float>(std::sin(ph));
      ph += step;
      m->process(ins, outs);
      if (i >= 19520) { acc += outs[0]; ++n; }
    }
    const double avg = acc / n;
    if (mode == 0) CHECK(avg == doctest::Approx(2.0 / std::numbers::pi).epsilon(0.02));
    else           CHECK(avg == doctest::Approx(1.0 / std::sqrt(2.0)).epsilon(0.02));
  }
}

TEST_CASE("follow: silence produces exact-zero envelope and low gate") {
  auto m = make_follow(48000.0, "peak");
  float ins[4] = {0.0f, 0.01f, 0.1f, 0.1f};
  float outs[2] = {9.9f, 9.9f};
  m->process(ins, outs);
  CHECK(outs[0] == 0.0f);
  CHECK(outs[1] == 0.0f);
}

TEST_CASE("follow: gate has hysteresis around thresh") {
  auto m = make_follow(48000.0, "peak");
  float outs[2] = {0.0f, 0.0f};
  auto settle = [&](float x) {
    float ins[4] = {x, 0.001f, 0.001f, 0.5f};  // thresh 0.5, fast so env ~= |in|
    for (int i = 0; i < 5000; ++i) m->process(ins, outs);
  };
  settle(0.0f);  CHECK(outs[1] == 0.0f);  // below off-threshold: low
  settle(0.4f);  CHECK(outs[1] == 0.0f);  // hold zone (0.375..0.5) from below: stays low
  settle(0.6f);  CHECK(outs[1] == 1.0f);  // above thresh: high
  settle(0.4f);  CHECK(outs[1] == 1.0f);  // hold zone from above: stays high
  settle(0.3f);  CHECK(outs[1] == 0.0f);  // below 0.75*thresh: low
}

TEST_CASE("follow: hostile inputs stay finite") {
  for (double rate : {44100.0, 48000.0, 96000.0}) {
    auto m = make_follow(rate, "rms");
    float outs[2] = {0.0f, 0.0f};
    float bad[4] = {NAN, INFINITY, -1.0f, NAN};
    m->process(bad, outs);
    CHECK(std::isfinite(outs[0]));
    CHECK(std::isfinite(outs[1]));
    float big[4] = {1e12f, 0.01f, 0.1f, 0.1f};
    m->process(big, outs);
    CHECK(std::isfinite(outs[0]));
    CHECK(std::isfinite(outs[1]));
  }
}
