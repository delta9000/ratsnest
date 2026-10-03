#include <doctest/doctest.h>
#include <cmath>
#include "dsp/modules.h"

using namespace ms::dsp;

namespace {
struct Lfo {
  std::unique_ptr<Module> m;
  float ins[3] = {1.0f, 0.0f, 0.0f};  // freq, reset, phase
  explicit Lfo(const char* wave) {
    const ModuleInfo* info = find_module("lfo");
    REQUIRE(info != nullptr);
    m = info->create();
    Config cfg;
    cfg.push_back({"wave", ConfigValue{wave, 0, {}, false}});
    m->init(48000.0, cfg);
  }
  float tick() { float o[1]; m->process(ins, o); return o[0]; }
};
}  // namespace

TEST_CASE("sine lfo completes one cycle at 1 Hz") {
  Lfo l("sine");
  float first = l.tick();
  for (int i = 1; i < 48000; ++i) l.tick();
  float wrapped = l.tick();
  CHECK(wrapped == doctest::Approx(first).epsilon(0.01));
}

TEST_CASE("square is bipolar with 50% duty") {
  Lfo l("square");
  int hi = 0;
  for (int i = 0; i < 48000; ++i)
    if (l.tick() > 0.0f) ++hi;
  CHECK(hi == doctest::Approx(24000).epsilon(0.01));
}

TEST_CASE("reset rising edge restarts the cycle") {
  Lfo l("saw");
  for (int i = 0; i < 12000; ++i) l.tick();  // quarter cycle in
  l.ins[1] = 1.0f;
  float after = l.tick();
  CHECK(after == doctest::Approx(-1.0f).epsilon(0.01));  // saw restarts at -1
}

TEST_CASE("phase offset shifts start point") {
  Lfo l("saw");
  l.ins[2] = 0.5f;
  CHECK(l.tick() == doctest::Approx(0.0f).epsilon(0.01));  // saw at half phase = 0
}

TEST_CASE("slow LFOs keep their period and do not stall") {
  for (float freq : {0.0033333f, 0.001f}) {
    CAPTURE(freq);
    Lfo l("saw");
    l.ins[0] = freq;
    // At 3/4 cycle, even a 1000-second oscillator must still advance.
    const int samples = static_cast<int>(0.75 * 48000.0 / freq);
    for (int i = 0; i < samples; ++i) l.tick();
    CHECK(l.tick() == doctest::Approx(0.5).epsilon(0.00001));
  }
}
