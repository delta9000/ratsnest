// `render --tap`: capture nodes added to the patch text, read per sample.
#include <doctest/doctest.h>
#include <string>
#include <vector>
#include "engine/engine.h"
#include "graph/tap.h"

using namespace ms;

namespace {

// A feedback echo (a cycle the scheduler must break) into a stereo mixer.
const char* kPatch = R"(
osc = vco { freq: 110hz, wave: saw }
amp = vca { gain: 0.4 }
dly = delay { time: 3ms, feedback: 0.5, mix: 1 }
bus = mix { levels: [1, 1], pans: [-0.5, 0.5] }
osc -> amp.in : 0.5
amp -> dly.in
dly -> amp.in : 0.25
amp -> bus.in1
dly -> bus.in2
bus.out_l -> out.left
bus.out_r -> out.right)";

struct Run {
  std::vector<float> l, r;
  std::vector<std::vector<float>> taps;  // l, r per tap
};

Run render(std::vector<graph::Tap>& taps, size_t frames) {
  auto b = graph::build_patch(kPatch, "t.patch", 48000.0);
  REQUIRE(b.compiled.ok());
  REQUIRE(graph::add_taps(b, kPatch, "t.patch", 48000.0, taps).empty());
  engine::Engine e(std::move(b.compiled.graph));
  Run run{std::vector<float>(frames), std::vector<float>(frames), {}};
  run.taps.assign(taps.size() * 2, std::vector<float>(frames));
  for (size_t f = 0; f < frames; ++f) {
    e.process(&run.l[f], &run.r[f], 1);
    for (size_t t = 0; t < taps.size(); ++t) {
      run.taps[2 * t][f] = e.output_value(static_cast<size_t>(taps[t].node_l), 0);
      run.taps[2 * t + 1][f] = e.output_value(static_cast<size_t>(taps[t].node_r), 0);
    }
  }
  return run;
}

}  // namespace

TEST_CASE("tap: NAME or NAME=endpoint") {
  const graph::Tap a = graph::parse_tap("kick");
  CHECK(a.name == "kick");
  CHECK(a.endpoint == "kick");
  const graph::Tap b = graph::parse_tap("low=bass_s.l");
  CHECK(b.name == "low");
  CHECK(b.endpoint == "bass_s.l");
}

TEST_CASE("tap: captures endpoints exactly and leaves the mix bit-identical") {
  std::vector<graph::Tap> none;
  const Run plain = render(none, 4800);
  std::vector<graph::Tap> taps{graph::parse_tap("bus"), graph::parse_tap("echo=dly"),
                               graph::parse_tap("left=bus.out_l")};
  const Run tapped = render(taps, 4800);
  CHECK(tapped.l == plain.l);
  CHECK(tapped.r == plain.r);
  CHECK(taps[0].stereo);          // mix has out_l/out_r
  CHECK_FALSE(taps[1].stereo);    // delay's default output
  CHECK_FALSE(taps[2].stereo);    // an explicit port is mono
  // The bus feeds `out` directly at unity, so its tap is the mix itself.
  CHECK(tapped.taps[0] == plain.l);
  CHECK(tapped.taps[1] == plain.r);
  CHECK(tapped.taps[4] == plain.l);
  CHECK(tapped.taps[2] == tapped.taps[3]);  // mono: one signal on both
  bool echo = false;
  for (float v : tapped.taps[2]) echo = echo || v != 0.0f;
  CHECK(echo);
}

TEST_CASE("tap: an endpoint that does not resolve names the tap") {
  auto b = graph::build_patch(kPatch, "t.patch", 48000.0);
  REQUIRE(b.compiled.ok());
  std::vector<graph::Tap> taps{graph::parse_tap("e=dlay")};
  const std::string err = graph::add_taps(b, kPatch, "t.patch", 48000.0, taps);
  CHECK(err.find("--tap e=dlay: ") == 0);
  CHECK_MESSAGE(err.find("did you mean 'dly'") != std::string::npos, err);
  taps = {graph::parse_tap("p=bus.nope")};
  CHECK(graph::add_taps(b, kPatch, "t.patch", 48000.0, taps).find("--tap p=bus.nope") == 0);
}
