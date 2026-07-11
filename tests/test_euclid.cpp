#include <array>
#include <string_view>
#include <doctest/doctest.h>
#include "dsp/modules.h"

using namespace ms::dsp;

namespace {
struct EuclidRig {
  std::unique_ptr<Module> module;
  float ins[5] = {0.0f, 0.0f, 8.0f, 3.0f, 0.0f};

  EuclidRig() {
    const ModuleInfo* info = find_module("euclid");
    REQUIRE(info != nullptr);
    module = info->create();
    module->init(48000.0, {});
  }

  std::array<float, 2> tick(float clock) {
    ins[0] = clock;
    std::array<float, 2> out{};
    module->process(ins, out.data());
    return out;
  }

  std::array<float, 2> edge() {
    (void)tick(0.0f);
    return tick(1.0f);
  }
};
}  // namespace

TEST_CASE("euclid registers live parameters and trigger plus gate outputs") {
  const ModuleInfo* info = find_module("euclid");
  REQUIRE(info != nullptr);
  REQUIRE(info->inputs.size() == 5);
  CHECK(std::string_view(info->inputs[0].name) == "clock");
  CHECK(std::string_view(info->inputs[1].name) == "reset");
  CHECK(std::string_view(info->inputs[2].name) == "steps");
  CHECK(info->inputs[2].def == doctest::Approx(16.0f));
  CHECK(std::string_view(info->inputs[3].name) == "hits");
  CHECK(info->inputs[3].def == doctest::Approx(4.0f));
  CHECK(std::string_view(info->inputs[4].name) == "rot");
  REQUIRE(info->outputs.size() == 2);
  CHECK(std::string_view(info->outputs[0].name) == "trig");
  CHECK(std::string_view(info->outputs[1].name) == "gate");
}

TEST_CASE("euclid distributes three hits over eight steps") {
  EuclidRig e;
  const int expected[8] = {1, 0, 0, 1, 0, 0, 1, 0};
  for (int hit : expected) {
    const auto out = e.edge();
    CHECK(out[0] == doctest::Approx(static_cast<float>(hit)));
    CHECK(out[1] == doctest::Approx(static_cast<float>(hit)));
  }
}

TEST_CASE("euclid trigger lasts one sample and gate follows clock width") {
  EuclidRig e;
  auto out = e.tick(1.0f);  // step zero is a hit
  CHECK(out[0] == doctest::Approx(1.0f));
  CHECK(out[1] == doctest::Approx(1.0f));
  out = e.tick(1.0f);
  CHECK(out[0] == doctest::Approx(0.0f));
  CHECK(out[1] == doctest::Approx(1.0f));
  out = e.tick(0.0f);
  CHECK(out[0] == doctest::Approx(0.0f));
  CHECK(out[1] == doctest::Approx(0.0f));
}

TEST_CASE("euclid handles zero hits and all hits") {
  EuclidRig e;
  e.ins[3] = 0.0f;
  for (int i = 0; i < 8; ++i) CHECK(e.edge()[0] == doctest::Approx(0.0f));
  e.ins[3] = 8.0f;
  for (int i = 0; i < 8; ++i) CHECK(e.edge()[0] == doctest::Approx(1.0f));
}

TEST_CASE("euclid rounds clamps and rotates live parameters") {
  EuclidRig e;
  e.ins[2] = 7.6f;  // rounds to 8
  e.ins[3] = 2.6f;  // rounds to 3
  e.ins[4] = 2.0f;
  const int rotated[8] = {0, 1, 0, 0, 1, 0, 1, 0};
  for (int hit : rotated)
    CHECK(e.edge()[0] == doctest::Approx(static_cast<float>(hit)));

  e.ins[2] = 0.0f;   // clamps to one step
  e.ins[3] = 99.0f;  // clamps to all hits
  e.ins[4] = -99.0f;
  CHECK(e.edge()[0] == doctest::Approx(1.0f));
}

TEST_CASE("euclid reset coincident with clock makes that edge step zero") {
  EuclidRig e;
  (void)e.edge();
  (void)e.edge();
  (void)e.tick(0.0f);
  e.ins[1] = 1.0f;
  const auto out = e.tick(1.0f);
  CHECK(out[0] == doctest::Approx(1.0f));
  CHECK(out[1] == doctest::Approx(1.0f));
  e.ins[1] = 0.0f;
  (void)e.tick(0.0f);
  CHECK(e.edge()[0] == doctest::Approx(0.0f));  // step one of E(3,8)
}
