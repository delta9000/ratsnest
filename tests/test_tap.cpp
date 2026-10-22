// `render --tap`: capture nodes added to the patch text, read per sample.
#include <doctest/doctest.h>
#include <filesystem>
#include <fstream>
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
      for (size_t c = 0; c < taps[t].nodes.size(); ++c)
        run.taps[2 * t + c][f] = e.output_value(static_cast<size_t>(taps[t].nodes[c]), 0);
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
  CHECK(taps[1].nodes.size() == 1);  // mono: one capture node
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

// A pan pair driven by one source as a sub file with stereo outputs, and a
// sub wrapping it that exposes one output.
struct SubFiles {
  std::filesystem::path dir = std::filesystem::temp_directory_path() / "ratsnest-test-tap";
  SubFiles() {
    std::filesystem::create_directories(dir);
    std::ofstream(dir / "st.sub") << "l = output {}\nr = output {}\nosc = vco { freq: 220hz }\n"
                                     "amp = vca { gain: 0.5 }\nosc -> l.in\nosc -> amp.in\namp -> r.in\n";
    std::ofstream(dir / "wrap.sub") << "main = output {}\ninner = sub { file: \"st.sub\" }\n"
                                       "inner.l -> main.in\n";
  }
};

TEST_CASE("tap: a sub's l/r outputs are stereo; one build, one node per channel") {
  const SubFiles files;
  const std::string patch = (files.dir / "t.patch").string();
  const char* src =
      "a = sub { file: \"st.sub\" }\nw = sub { file: \"wrap.sub\" }\n"
      "a.l -> out.left\na.r -> out.right\nw -> out.left : 0.1\n";
  auto b = graph::build_patch(src, patch, 48000.0);
  REQUIRE_MESSAGE(b.compiled.ok(), b.compiled.errors[0].msg);
  const size_t before = b.compiled.graph->nodes.size();
  std::vector<graph::Tap> taps{graph::parse_tap("a"), graph::parse_tap("w"),
                               graph::parse_tap("osc=a.osc")};
  const std::string err = graph::add_taps(b, src, patch, 48000.0, taps);
  REQUIRE_MESSAGE(err.empty(), err);
  CHECK(taps[0].stereo);
  CHECK_FALSE(taps[1].stereo);  // wraps a stereo sub, but exposes only main
  CHECK_FALSE(taps[2].stereo);
  CHECK(b.compiled.graph->nodes.size() == before + 4);  // 2 + 1 + 1
  CHECK(taps[1].nodes.size() == 1);
  engine::Engine e(std::move(b.compiled.graph));
  std::vector<float> l(256), r(256);
  for (size_t f = 0; f < l.size(); ++f) {
    e.process(&l[f], &r[f], 1);
    CHECK(e.output_value(static_cast<size_t>(taps[0].nodes[0]), 0) == r[f] * 2.0f);
    CHECK(e.output_value(static_cast<size_t>(taps[0].nodes[1]), 0) == r[f]);
  }
}

TEST_CASE("tap: a failing tap leaves the build as it was and is named") {
  auto b = graph::build_patch(kPatch, "t.patch", 48000.0);
  REQUIRE(b.compiled.ok());
  const size_t before = b.compiled.graph->nodes.size();
  std::vector<graph::Tap> taps{graph::parse_tap("ok=bus"), graph::parse_tap("bad=bus.nope"),
                               graph::parse_tap("fine=dly")};
  CHECK(graph::add_taps(b, kPatch, "t.patch", 48000.0, taps).find("--tap bad=bus.nope: ") == 0);
  REQUIRE(b.compiled.ok());
  CHECK(b.compiled.graph->nodes.size() == before);
}

TEST_CASE("tap: names must be distinct plain file-name parts") {
  using graph::parse_tap;
  CHECK(graph::check_taps({parse_tap("kick"), parse_tap("pad=pad_s.l")}).empty());
  CHECK(graph::check_taps({parse_tap("=kick")}).find("needs a name") != std::string::npos);
  CHECK(graph::check_taps({parse_tap("k=")}).find("needs an endpoint") != std::string::npos);
  for (const char* bad : {"a/b=kick", "..=kick", "x..y=kick", "a\\b=kick", "../up=kick"}) {
    CAPTURE(bad);
    CHECK(graph::check_taps({parse_tap(bad)}).find("may not contain") != std::string::npos);
  }
  const std::string dup = graph::check_taps({parse_tap("kick"), parse_tap("kick=bd")});
  CHECK(dup.find("'kick' is given twice") != std::string::npos);
}
