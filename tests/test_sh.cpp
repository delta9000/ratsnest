#include <doctest/doctest.h>
#include "dsp/modules.h"

using namespace ms::dsp;

namespace {
struct ShRig {
  std::unique_ptr<Module> module;
  float ins[2] = {0.0f, 0.0f};  // in, trig

  explicit ShRig(const char* mode) {
    const ModuleInfo* info = find_module("sh");
    REQUIRE(info != nullptr);
    module = info->create();
    Config cfg;
    ConfigValue v; v.str = mode;
    cfg.push_back({"mode", v});
    module->init(48000.0, cfg);
  }

  float tick(float in, float trig) {
    ins[0] = in;
    ins[1] = trig;
    float out[1];
    module->process(ins, out);
    return out[0];
  }
};
}  // namespace

TEST_CASE("sample mode latches only on the trigger's rising edge") {
  ShRig sh("sample");
  CHECK(sh.tick(0.3f, 0.0f) == doctest::Approx(0.0f));  // nothing latched yet
  CHECK(sh.tick(0.3f, 1.0f) == doctest::Approx(0.3f));  // edge: latch
  CHECK(sh.tick(0.9f, 1.0f) == doctest::Approx(0.3f));  // held high: no re-latch
  CHECK(sh.tick(0.9f, 0.0f) == doctest::Approx(0.3f));  // low: hold
  CHECK(sh.tick(-0.4f, 1.0f) == doctest::Approx(-0.4f));  // next edge: latch again
}

TEST_CASE("track mode follows while the gate is high and holds when low") {
  ShRig sh("track");
  CHECK(sh.tick(0.2f, 1.0f) == doctest::Approx(0.2f));
  CHECK(sh.tick(0.6f, 1.0f) == doctest::Approx(0.6f));  // following
  CHECK(sh.tick(0.9f, 0.0f) == doctest::Approx(0.6f));  // gate low: hold
  CHECK(sh.tick(-0.1f, 0.0f) == doctest::Approx(0.6f));
  CHECK(sh.tick(-0.1f, 1.0f) == doctest::Approx(-0.1f));  // reopens
}
