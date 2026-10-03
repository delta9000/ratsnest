#include <doctest/doctest.h>
#include <cmath>
#include "dsp/modules.h"

using namespace ms::dsp;
TEST_CASE("dust produces seeded bipolar impulses at the requested average rate") {
  const auto* info = find_module("dust");
  REQUIRE(info != nullptr);
  auto a = info->create(), b = info->create();
  a->init(48000, {}); b->init(48000, {});
  float ins[1] = {1000}, oa[2], ob[2];
  int events = 0;
  double sum = 0;
  for (int i = 0; i < 480000; ++i) {
    a->process(ins, oa); b->process(ins, ob);
    REQUIRE(oa[0] == ob[0]);
    REQUIRE(oa[1] == ob[1]);
    if (oa[1] > 0.5f) { ++events; sum += oa[0]; }
    else REQUIRE(oa[0] == 0.f);
    REQUIRE(std::abs(oa[0]) <= 1.f);
  }
  CHECK(events > 9500);
  CHECK(events < 10500);
  CHECK(std::abs(sum / events) < 0.03);
  ins[0] = 0;
  a->process(ins, oa);
  CHECK(oa[0] == 0.f);
  CHECK(oa[1] == 0.f);
  b->copy_state_from(*a);
  ins[0] = 48000;
  for (int i = 0; i < 100; ++i) {
    a->process(ins, oa); b->process(ins, ob);
    CHECK(oa[0] == ob[0]);
    CHECK(oa[1] == 1.f);
  }
}
