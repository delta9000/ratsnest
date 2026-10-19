#include <doctest/doctest.h>
#include "dsp/modules.h"

using namespace ms::dsp;

namespace {
struct QuantRig {
  std::unique_ptr<Module> module;
  float ins[6] = {0.0f, 0.0f, 0.0f, 0.0f, 4095.0f, 0.0f};  // in, root, transpose

  explicit QuantRig(std::vector<double> scale = {}) {
    const ModuleInfo* info = find_module("quant");
    REQUIRE(info != nullptr);
    module = info->create();
    Config cfg;
    if (!scale.empty()) {
      ConfigValue v; v.arr = std::move(scale);
      cfg.push_back({"scale", v});
    }
    module->init(48000.0, cfg);
  }

  void tick(float in, float (&out)[2]) {
    ins[0] = in;
    module->process(ins, out);
  }
};
}  // namespace

TEST_CASE("default chromatic scale rounds to the nearest semitone") {
  QuantRig q;
  float out[2];
  q.tick(0.26f, out);  // 3.12 semis -> 3 semis = 0.25 v_oct
  CHECK(out[0] == doctest::Approx(0.25f));
  q.tick(-1.0f, out);
  CHECK(out[0] == doctest::Approx(-1.0f));
}

TEST_CASE("scale array restricts degrees across octaves") {
  QuantRig q({0.0, 7.0});  // root + fifth
  float out[2];
  q.tick(2.0f / 12.0f, out);  // 2 semis: nearest of {0,7,-5,12} is 0
  CHECK(out[0] == doctest::Approx(0.0f));
  q.tick(6.0f / 12.0f, out);  // 6 semis: 7 is 1 away, 0 is 6 away
  CHECK(out[0] == doctest::Approx(7.0f / 12.0f));
  q.tick(11.0f / 12.0f, out);  // 11 semis: 12 (next octave root) wins
  CHECK(out[0] == doctest::Approx(1.0f));
  q.tick(-3.0f / 12.0f, out);  // -3 semis: -5 is 2 away, 0 is 3 away
  CHECK(out[0] == doctest::Approx(-5.0f / 12.0f));
}

TEST_CASE("root shifts the scale and transpose sums pre-quantize") {
  QuantRig q({0.0, 4.0, 7.0});  // major triad
  q.ins[1] = 2.0f;              // root = D
  float out[2];
  q.tick(0.0f, out);  // 0 semis, scale-relative -2 -> nearest degree 0 -> +root
  CHECK(out[0] == doctest::Approx(2.0f / 12.0f));
  q.ins[2] = 1.0f;  // transpose up an octave
  q.tick(0.0f, out);
  CHECK(out[0] == doctest::Approx(14.0f / 12.0f));
}

TEST_CASE("changed emits a one-sample gate per new degree, none at start") {
  QuantRig q;
  float out[2];
  q.tick(0.0f, out);
  CHECK(out[1] == doctest::Approx(0.0f));  // first sample: no gate
  q.tick(0.0f, out);
  CHECK(out[1] == doctest::Approx(0.0f));  // unchanged
  q.tick(0.25f, out);                      // new degree
  CHECK(out[1] == doctest::Approx(1.0f));
  q.tick(0.25f, out);
  CHECK(out[1] == doctest::Approx(0.0f));  // gate is one sample wide
}
