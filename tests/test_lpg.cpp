#include <doctest/doctest.h>
#include <cmath>
#include "dsp/modules.h"

using namespace ms::dsp;

namespace {
struct LpgRig {
  std::unique_ptr<Module> module;
  // in, cv, strike, damp, decay
  float ins[5] = {0.0f, 0.0f, 0.0f, 0.5f, 0.05f};

  LpgRig() {
    const ModuleInfo* info = find_module("lpg");
    REQUIRE(info != nullptr);
    module = info->create();
    module->init(48000.0, {});
  }

  float tick() {
    float out[1];
    module->process(ins, out);
    return out[0];
  }
};
}  // namespace

TEST_CASE("lpg is silent with no cv and no strike") {
  LpgRig lpg;
  for (int i = 0; i < 1000; ++i) {
    lpg.ins[0] = i % 2 == 0 ? 1.0f : -1.0f;
    CHECK(std::abs(lpg.tick()) < 1e-4f);
  }
}

TEST_CASE("lpg opens with cv and passes signal") {
  LpgRig lpg;
  lpg.ins[1] = 1.0f;  // cv high
  double sum = 0.0;
  for (int i = 0; i < 4800; ++i) {
    lpg.ins[0] = std::sin(static_cast<float>(2.0 * 3.14159265358979 * 220.0 * i / 48000.0));
    const float o = lpg.tick();
    if (i > 2400) sum += static_cast<double>(o) * o;
  }
  CHECK(std::sqrt(sum / 2400.0) > 0.3);
}

TEST_CASE("lpg strike plucks then decays back to silence") {
  LpgRig lpg;
  lpg.ins[4] = 0.02f;  // 20 ms decay
  // Feed a constant tone; strike once.
  auto energy = [&](int n) {
    double s = 0.0;
    for (int i = 0; i < n; ++i) {
      lpg.ins[0] = std::sin(static_cast<float>(2.0 * 3.14159265358979 * 220.0 * i / 48000.0));
      const float o = lpg.tick();
      s += static_cast<double>(o) * o;
    }
    return std::sqrt(s / n);
  };
  lpg.ins[2] = 1.0f;  // strike edge on first sample
  const double open = energy(480);   // first 10 ms: gate open
  lpg.ins[2] = 0.0f;
  (void)energy(9600);                // 200 ms: ten decay constants pass
  const double closed = energy(480);
  CHECK(open > 0.1);
  CHECK(closed < open * 0.01);
}

TEST_CASE("lpg damp darkens the output") {
  LpgRig bright;
  LpgRig dark;
  bright.ins[1] = dark.ins[1] = 1.0f;   // both fully open
  bright.ins[3] = 0.0f;
  dark.ins[3] = 1.0f;
  // High-frequency content comparison at 6 kHz.
  double sb = 0.0, sd = 0.0;
  for (int i = 0; i < 9600; ++i) {
    const float x = std::sin(static_cast<float>(2.0 * 3.14159265358979 * 6000.0 * i / 48000.0));
    bright.ins[0] = x;
    dark.ins[0] = x;
    const float ob = bright.tick();
    const float od = dark.tick();
    if (i > 4800) {
      sb += static_cast<double>(ob) * ob;
      sd += static_cast<double>(od) * od;
    }
  }
  CHECK(sd < sb * 0.25);
}

TEST_CASE("lpg strike ramps open over 2 ms instead of stepping") {
  // A 55 Hz bass tone through a closed gate: the 30 Hz floor filter still
  // tracks it, so an instant strike stepped the output by ~0.2.
  LpgRig lpg;
  lpg.ins[4] = 0.3f;
  float prev = 0.0f, event = 0.0f, steady = 0.0f, peak = 0.0f;
  for (int i = 0; i < 9600; ++i) {
    lpg.ins[0] = std::sin(static_cast<float>(2.0 * 3.14159265358979 * 55.0 * i / 48000.0));
    lpg.ins[2] = i >= 4800 ? 1.0f : 0.0f;
    const float o = lpg.tick();
    const float step = std::abs(o - prev);
    if (i >= 4800 && i < 4800 + 480) event = std::max(event, step);
    if (i >= 4800 + 480) steady = std::max(steady, step);
    if (i >= 4800) peak = std::max(peak, std::abs(o));
    prev = o;
  }
  CHECK(peak > 0.5f);                 // still a full-strength pluck
  // An instant strike stepped by ~0.22 (31x the tone's slope); the ramp
  // leaves only the filter catching up as it opens, ~3x.
  CHECK(steady == doctest::Approx(0.0072f).epsilon(0.05));  // 2*pi*55/48000
  CHECK(event < 4.0f * steady);
}

TEST_CASE("lpg strike reaches fully open in 2 ms") {
  LpgRig lpg;
  lpg.ins[4] = 1.0f;
  lpg.ins[0] = 1.0f;  // DC in: the output is the filter state times the gate
  for (int i = 0; i < 48000; ++i) lpg.tick();  // closed filter settles to 1
  lpg.ins[2] = 1.0f;
  float o = 0.0f;
  for (int i = 0; i < 95; ++i) o = lpg.tick();
  CHECK(o < 0.999f);
  for (int i = 0; i < 2; ++i) o = lpg.tick();
  CHECK(o == doctest::Approx(1.0f).epsilon(1e-3));
}
