#include <doctest/doctest.h>
#include <cmath>
#include "dsl/parser.h"
#include "engine/audio_safety.h"
#include "engine/engine.h"
#include "engine/pipewire_out.h"
#include <limits>
#include <string>
#include "graph/graph.h"

using namespace ms;

static engine::Engine make_engine(const char* src) {
  auto p = dsl::parse(src);
  REQUIRE(p.ok());
  auto c = graph::compile(p.ast, 48000.0);
  REQUIRE(c.ok());
  return engine::Engine(std::move(c.graph));
}

TEST_CASE("passes a constant through vca to out") {
  auto e = make_engine("amp = vca { in: 0.5 }\namp -> out.in\n");
  float l[4], r[4];
  e.process(l, r, 4);
  for (int i = 0; i < 4; ++i) {
    CHECK(l[i] == doctest::Approx(0.5f));
    CHECK(r[i] == doctest::Approx(0.5f));
  }
}

TEST_CASE("cable depth scales the signal") {
  auto e = make_engine("amp = vca { in: 1.0 }\namp -> out.in : 0.25\n");
  float l[1], r[1];
  e.process(l, r, 1);
  CHECK(l[0] == doctest::Approx(0.25f));
}

TEST_CASE("multiple cables into one port sum") {
  auto e = make_engine(
      "a = vca { in: 0.2 }\nb = vca { in: 0.3 }\na -> out.in\nb -> out.in\n");
  float l[1], r[1];
  e.process(l, r, 1);
  CHECK(l[0] == doctest::Approx(0.5f));
}

TEST_CASE("feedback edge delivers previous sample (one-sample delay)") {
  // a.cv fed by b, which follows a: a(gain 1 + cv) ramps +1 per sample.
  auto e = make_engine(
      "a = vca { in: 1.0 }\nb = vca\na -> b\nb -> a.cv\na -> out.in\n");
  float l[3], r[3];
  e.process(l, r, 3);
  CHECK(l[0] == doctest::Approx(1.0f));  // cv sees b's initial 0
  CHECK(l[1] == doctest::Approx(2.0f));  // cv sees b's previous-sample 1
  CHECK(l[2] == doctest::Approx(3.0f));  // float headroom exposes the ramp
}

TEST_CASE("feedback ramp observed below clip") {
  auto e = make_engine(
      "a = vca { in: 0.1 }\nb = vca\na -> b\nb -> a.cv\na -> out.in\n");
  float l[3], r[3];
  e.process(l, r, 3);
  CHECK(l[0] == doctest::Approx(0.1f));            // gain 1 + cv 0
  CHECK(l[1] == doctest::Approx(0.1f * 1.1f));     // cv = 0.1 (prev sample)
  CHECK(l[2] == doctest::Approx(0.1f * 1.11f));    // cv = 0.11
}

TEST_CASE("graph output preserves finite float headroom") {
  auto e = make_engine("amp = vca { in: 3.0 }\namp -> out.in\n");
  float l[1], r[1];
  e.process(l, r, 1);
  CHECK(l[0] == doctest::Approx(3.0f));
  CHECK(r[0] == doctest::Approx(3.0f));
}

TEST_CASE("device boundary contains overload and non-finite samples") {
  CHECK(engine::device_sample(3.0f) == 1.0f);
  CHECK(engine::device_sample(-3.0f) == -1.0f);
  CHECK(engine::device_sample(0.25f) == 0.25f);
  CHECK(engine::device_sample(std::numeric_limits<float>::infinity()) == 0.0f);
  CHECK(engine::device_sample(std::numeric_limits<float>::quiet_NaN()) == 0.0f);
}

TEST_CASE("audio device safety clamps and sanitizes its final buffers") {
  float l[4] = {2.0f, -2.0f, NAN, 0.5f};
  float r[4] = {-4.0f, 4.0f, INFINITY, -0.5f};
  engine::clamp_device_buffer(l, r, 4);
  CHECK(l[0] == 1.0f);
  CHECK(l[1] == -1.0f);
  CHECK(l[2] == 0.0f);
  CHECK(l[3] == 0.5f);
  CHECK(r[0] == -1.0f);
  CHECK(r[1] == 1.0f);
  CHECK(r[2] == 0.0f);
  CHECK(r[3] == -0.5f);
}

TEST_CASE("a non-finite module output reads as zero, cabled or not") {
  // 3e38 * 8 overflows to +inf inside each vca. `loud` feeds out; `spare`
  // feeds nothing and is only ever looked at through output_value.
  const std::string big = "3" + std::string(38, '0');
  auto e = make_engine(("loud = vca { in: " + big + ", gain: 8 }\nspare = vca { in: " + big +
                        ", gain: 8 }\nloud -> out.in\n").c_str());
  float l[8], r[8];
  e.process(l, r, 8);
  for (float v : l) CHECK(v == 0.0f);
  for (size_t i = 0; i < e.graph().nodes.size(); ++i) CHECK(e.output_value(i, 0) == 0.0f);
}
