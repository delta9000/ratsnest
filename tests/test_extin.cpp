#include <doctest/doctest.h>
#include <string>
#include "dsp/modules.h"

using namespace ms::dsp;

namespace {
std::unique_ptr<Module> make_extin() {
  const ModuleInfo* info = find_module("extin");
  REQUIRE(info != nullptr);
  auto m = info->create();
  m->init(48000.0, {});
  return m;
}
}  // namespace

TEST_CASE("extin: metadata opts into audio capture") {
  const ModuleInfo* info = find_module("extin");
  REQUIRE(info != nullptr);
  CHECK(info->wants_audio_in == true);
  CHECK(info->inputs.size() == 0);
  REQUIRE(info->outputs.size() == 1);
  CHECK(std::string(info->outputs[0].name) == "out");
}

TEST_CASE("extin: emits captured samples in order, silence on underrun") {
  auto m = make_extin();
  const float chunk[3] = {0.1f, 0.2f, 0.3f};
  m->on_audio_in(chunk, 3);

  float outs[1];
  m->process(nullptr, outs);
  CHECK(outs[0] == doctest::Approx(0.1f));
  m->process(nullptr, outs);
  CHECK(outs[0] == doctest::Approx(0.2f));
  m->process(nullptr, outs);
  CHECK(outs[0] == doctest::Approx(0.3f));
  m->process(nullptr, outs);
  CHECK(outs[0] == doctest::Approx(0.0f));  // ring empty -> silence
}

TEST_CASE("extin: outputs silence when no audio has been delivered (offline)") {
  auto m = make_extin();
  float outs[1] = {9.0f};
  for (int i = 0; i < 100; ++i) {
    m->process(nullptr, outs);
    CHECK(outs[0] == doctest::Approx(0.0f));
  }
}

TEST_CASE("extin: copy_state_from preserves pending samples across reload") {
  auto a = make_extin();
  const float c[2] = {0.5f, -0.5f};
  a->on_audio_in(c, 2);

  auto b = make_extin();
  b->copy_state_from(*a);
  float outs[1];
  b->process(nullptr, outs);
  CHECK(outs[0] == doctest::Approx(0.5f));
  b->process(nullptr, outs);
  CHECK(outs[0] == doctest::Approx(-0.5f));
}
