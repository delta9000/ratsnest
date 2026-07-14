#include <doctest/doctest.h>
#include <cmath>
#include <cstddef>
#include <filesystem>
#include <limits>
#include <numbers>
#include <string>
#include <vector>
#include "dsp/modules.h"
#include "io/wav.h"

using namespace ms::dsp;

namespace {

std::unique_ptr<Module> make_grains(double rate = 48000.0, double secs = 1.0,
                                    double seed = 1.0,
                                    const char* file = nullptr) {
  const ModuleInfo* info = find_module("grains");
  REQUIRE(info != nullptr);
  auto m = info->create();
  Config cfg;
  cfg.push_back({"secs", ConfigValue{"", secs, {}, true}});
  cfg.push_back({"seed", ConfigValue{"", seed, {}, true}});
  if (file) cfg.push_back({"file", ConfigValue{std::string(file), 0.0, {}, false}});
  m->init(rate, cfg);
  return m;
}

// Test-friendly baseline, NOT the module defaults: density/jitter/shape/spread
// all 0 so nothing happens unless a test asks for it. Index map:
// {in, freeze, trig, position, scatter, size, pitch, density, jitter, shape,
//  spread, reverse}
struct Ins {
  float v[12] = {0.f, 0.f, 0.f, 0.5f, 0.f, 0.1f, 0.f, 0.f, 0.f, 0.f, 0.f, 0.f};
};

}  // namespace

TEST_CASE("grains: metadata and defaults") {
  const ModuleInfo* info = find_module("grains");
  REQUIRE(info != nullptr);
  CHECK(std::string(info->type) == "grains");

  REQUIRE(info->inputs.size() == 12);
  const char* names[12] = {"in",      "freeze", "trig",  "position",
                           "scatter", "size",   "pitch", "density",
                           "jitter",  "shape",  "spread", "reverse"};
  const float defs[12] = {0.f, 0.f, 0.f, 0.5f, 0.f, 0.1f,
                          0.f, 10.f, 0.5f, 0.5f, 0.5f, 0.f};
  for (int i = 0; i < 12; ++i) {
    CHECK(std::string(info->inputs[i].name) == names[i]);
    CHECK(info->inputs[i].def == defs[i]);
  }

  REQUIRE(info->configs.size() == 3);
  CHECK(std::string(info->configs[0].name) == "secs");
  CHECK(std::string(info->configs[1].name) == "file");
  CHECK(std::string(info->configs[2].name) == "seed");

  REQUIRE(info->outputs.size() == 3);
  CHECK(std::string(info->outputs[0].name) == "out_l");
  CHECK(std::string(info->outputs[1].name) == "out_r");
  CHECK(std::string(info->outputs[2].name) == "onset");
}

TEST_CASE("grains: silence in, exact silence out") {
  auto m = make_grains();
  Ins ins;  // density 0: no internal seeding
  float outs[3] = {1.f, 1.f, 1.f};
  for (int i = 0; i < 10000; ++i) {
    m->process(ins.v, outs);
    CHECK(outs[0] == 0.0f);
    CHECK(outs[1] == 0.0f);
    CHECK(outs[2] == 0.0f);
  }
}

TEST_CASE("grains: a triggered grain reproduces the buffer under its window") {
  auto m = make_grains();  // 48 kHz, secs 1 -> ring 65536, write_abs starts 65536
  Ins ins;
  float outs[3] = {0.f, 0.f, 0.f};

  // Record a known ramp: value cycles 0..0.999 with period 1000.
  for (int i = 0; i < 48000; ++i) {
    ins.v[0] = static_cast<float>(i % 1000) * 0.001f;
    m->process(ins.v, outs);
  }

  // Freeze, then trig one boxcar grain at position 0 (newest).
  ins.v[0] = 0.0f;
  ins.v[1] = 1.0f;  // freeze
  ins.v[2] = 1.0f;  // trig
  ins.v[3] = 0.0f;  // position: newest
  m->process(ins.v, outs);
  CHECK(outs[2] == 1.0f);  // onset pulse
  ins.v[2] = 0.0f;

  // Placement math (mirrors the module): dur = span = 0.1 * 48000 = 4800;
  // available = 65536 - 2*4800 - 8 = 55928; delay = 0*available + 4800 + 4;
  // write_abs at seed = 65536 + 48000 = 113536 (the frozen sample wrote
  // nothing); start = 113536 - 4804 -> ramp index 43196. At grain sample t
  // the read index is start + t, i.e. ramp index 43196 + t.
  for (int t = 1; t < 4800; ++t) {
    m->process(ins.v, outs);
    if (t == 2400) {  // boxcar plateau, exact content expected
      const float expect = static_cast<float>((43196 + 2400) % 1000) * 0.001f *
                           std::cos(std::numbers::pi_v<float> / 4.0f);
      CHECK(outs[0] == doctest::Approx(expect).epsilon(0.001));
      CHECK(outs[1] == doctest::Approx(outs[0]).epsilon(0.0001));  // spread 0
    }
  }

  // After the grain ends: exact silence again.
  for (int t = 0; t < 100; ++t) m->process(ins.v, outs);
  CHECK(outs[0] == 0.0f);
  CHECK(outs[1] == 0.0f);
}

TEST_CASE("grains: pitch +1 doubles read speed") {
  auto run = [](float pitch) {
    auto m = make_grains();
    Ins ins;
    float outs[3] = {0.f, 0.f, 0.f};
    for (int i = 0; i < 48000; ++i) {
      ins.v[0] = static_cast<float>(
          std::sin(2.0 * std::numbers::pi * 100.0 * i / 48000.0));
      m->process(ins.v, outs);
    }
    ins.v[0] = 0.0f;
    ins.v[1] = 1.0f;
    ins.v[2] = 1.0f;
    ins.v[6] = pitch;
    m->process(ins.v, outs);
    ins.v[2] = 0.0f;
    int crossings = 0;
    float prev = 0.0f;
    for (int t = 1; t < 4800; ++t) {
      m->process(ins.v, outs);
      if ((prev < 0.0f && outs[0] > 0.0f) || (prev > 0.0f && outs[0] < 0.0f))
        ++crossings;
      if (outs[0] != 0.0f) prev = outs[0];
    }
    return crossings;
  };
  // 100 Hz over a 4800-sample grain: ~20 sign changes; pitch +1 reads the
  // buffer at 2x -> ~200 Hz -> ~40.
  const int base = run(0.0f);
  const int up = run(1.0f);
  CHECK(base >= 17);
  CHECK(up >= 2 * base - 4);
  CHECK(up <= 2 * base + 4);
}

TEST_CASE("grains: reverse grain plays a ramp backward") {
  auto m = make_grains();
  Ins ins;
  float outs[3] = {0.f, 0.f, 0.f};
  for (int i = 0; i < 48000; ++i) {
    ins.v[0] = static_cast<float>(i) * 1e-5f;  // strictly increasing
    m->process(ins.v, outs);
  }
  ins.v[0] = 0.0f;
  ins.v[1] = 1.0f;
  ins.v[2] = 1.0f;
  ins.v[11] = 1.0f;  // reverse
  m->process(ins.v, outs);
  ins.v[2] = 0.0f;
  float prev = 1e9f;
  for (int t = 1; t < 4800; ++t) {
    m->process(ins.v, outs);
    if (t >= 500 && t <= 4300) {  // boxcar plateau: env == 1 exactly
      CHECK(outs[0] < prev);
      prev = outs[0];
    }
  }
}

TEST_CASE("grains: freeze keeps loud input out of the buffer") {
  auto m = make_grains();
  Ins ins;
  float outs[3] = {0.f, 0.f, 0.f};
  ins.v[0] = 1.0f;  // loud input...
  ins.v[1] = 1.0f;  // ...but frozen from the very first sample
  for (int i = 0; i < 2000; ++i) m->process(ins.v, outs);
  ins.v[2] = 1.0f;
  m->process(ins.v, outs);
  ins.v[2] = 0.0f;
  for (int t = 0; t < 4800; ++t) {
    m->process(ins.v, outs);
    CHECK(outs[0] == 0.0f);  // the buffer only ever held silence
  }
}

TEST_CASE("grains: jitter 0 emits a perfectly periodic grain stream") {
  auto m = make_grains();
  Ins ins;
  ins.v[7] = 100.0f;  // density: 480-sample base interval at 48 kHz
  ins.v[8] = 0.0f;    // jitter 0: deterministic stream
  float outs[3] = {0.f, 0.f, 0.f};
  int last = -1;
  std::vector<int> deltas;
  for (int i = 0; i < 4800; ++i) {
    m->process(ins.v, outs);
    if (outs[2] == 1.0f) {
      if (last >= 0) deltas.push_back(i - last);
      last = i;
    }
  }
  REQUIRE(deltas.size() == 9);  // onsets at 0, 480, ..., 4320
  for (int d : deltas) CHECK(d == 480);
}

TEST_CASE("grains: equal seeds produce identical streams; different seeds differ") {
  auto run = [](double seed, std::vector<float>& out) {
    auto m = make_grains(48000.0, 1.0, seed);
    Ins ins;
    ins.v[7] = 40.0f;  // density
    ins.v[8] = 1.0f;   // full jitter: Poisson cloud
    ins.v[9] = 1.0f;   // Hann
    ins.v[10] = 1.0f;  // full spread
    float outs[3] = {0.f, 0.f, 0.f};
    // Warm-up: fill the ring so position 0.5 reads recorded content, not the
    // silent pre-roll (grains sit ~32768 samples behind the write head).
    for (int i = 0; i < 65536; ++i) {
      ins.v[0] = static_cast<float>(
          std::sin(2.0 * std::numbers::pi * 220.0 * i / 48000.0) * 0.5);
      m->process(ins.v, outs);
    }
    for (int i = 0; i < 20000; ++i) {
      ins.v[0] = static_cast<float>(
          std::sin(2.0 * std::numbers::pi * 220.0 * i / 48000.0) * 0.5);
      m->process(ins.v, outs);
      out.push_back(outs[0]);
      out.push_back(outs[1]);
    }
  };
  std::vector<float> a, b, c;
  run(7.0, a);
  run(7.0, b);
  run(8.0, c);
  CHECK(a == b);
  CHECK(a != c);
}

TEST_CASE("grains: saturated pool stays bounded and finite") {
  auto m = make_grains();
  Ins ins;
  ins.v[5] = 2.0f;    // 2 s grains (quarter-buffer cap applies)
  ins.v[7] = 200.0f;  // max density: pool saturates, seeds drop
  ins.v[8] = 1.0f;
  ins.v[9] = 0.0f;    // boxcar: worst-case overlap energy
  ins.v[10] = 1.0f;
  float outs[3] = {0.f, 0.f, 0.f};
  for (int i = 0; i < 30000; ++i) {
    ins.v[0] = static_cast<float>(
        std::sin(2.0 * std::numbers::pi * 220.0 * i / 48000.0));
    m->process(ins.v, outs);
    CHECK(std::isfinite(outs[0]));
    CHECK(std::isfinite(outs[1]));
    CHECK(std::abs(outs[0]) < 6.0f);
    CHECK(std::abs(outs[1]) < 6.0f);
  }
}

TEST_CASE("grains: scatter randomizes grain position") {
  auto m = make_grains();
  Ins ins;
  float outs[3] = {0.f, 0.f, 0.f};
  // Fill the ENTIRE ring (65536+) so every scattered position lands on
  // recorded, strictly increasing content — never the zero-initialized region.
  for (int i = 0; i < 70000; ++i) {
    ins.v[0] = static_cast<float>(i) * 1e-5f;  // strictly increasing ramp
    m->process(ins.v, outs);
  }
  ins.v[0] = 0.0f;
  ins.v[1] = 1.0f;   // freeze
  ins.v[4] = 1.0f;   // full scatter
  ins.v[5] = 0.01f;  // short grains (480 samples)
  auto grain_peak = [&]() {
    ins.v[2] = 1.0f;
    m->process(ins.v, outs);
    ins.v[2] = 0.0f;
    float peak = 0.0f;
    for (int t = 0; t < 480; ++t) {
      m->process(ins.v, outs);
      peak = std::max(peak, std::abs(outs[0]));
    }
    return peak;
  };
  // Deterministic given the seed: two draws land on different ramp values.
  const float p1 = grain_peak();
  const float p2 = grain_peak();
  CHECK(p1 != p2);
}

TEST_CASE("grains: hostile inputs never poison state") {
  auto m = make_grains();
  Ins ins;
  float outs[3] = {0.f, 0.f, 0.f};
  const float bad[3] = {std::numeric_limits<float>::quiet_NaN(),
                        std::numeric_limits<float>::infinity(),
                        -std::numeric_limits<float>::infinity()};
  for (int k = 0; k < 3; ++k) {
    for (int i = 0; i < 12; ++i) ins.v[i] = bad[k];
    for (int i = 0; i < 500; ++i) {
      m->process(ins.v, outs);
      CHECK(std::isfinite(outs[0]));
      CHECK(std::isfinite(outs[1]));
    }
  }
  // Recovery: normal recording and a clean grain afterwards.
  ins = Ins{};
  float outs2[3] = {0.f, 0.f, 0.f};
  for (int i = 0; i < 48000; ++i) {
    ins.v[0] = static_cast<float>(i % 1000) * 0.001f;
    m->process(ins.v, outs2);
  }
  ins.v[0] = 0.0f;
  ins.v[1] = 1.0f;
  ins.v[2] = 1.0f;
  m->process(ins.v, outs2);
  ins.v[2] = 0.0f;
  bool nonzero = false;
  for (int t = 1; t < 4800; ++t) {
    m->process(ins.v, outs2);
    CHECK(std::isfinite(outs2[0]));
    if (outs2[0] != 0.0f) nonzero = true;
  }
  CHECK(nonzero);
}

TEST_CASE("grains: frozen buffer is immune to input (twin test)") {
  auto a = make_grains();
  auto b = make_grains();
  Ins ia, ib;
  float oa[3] = {0.f, 0.f, 0.f};
  float ob[3] = {0.f, 0.f, 0.f};
  for (int i = 0; i < 48000; ++i) {
    const float x = static_cast<float>(
        std::sin(2.0 * std::numbers::pi * 220.0 * i / 48000.0) * 0.5);
    ia.v[0] = x;
    ib.v[0] = x;
    a->process(ia.v, oa);
    b->process(ib.v, ob);
  }
  ia.v[1] = 1.0f;
  ib.v[1] = 1.0f;
  ia.v[0] = 1.0f;  // a gets slammed while frozen; b hears silence
  ib.v[0] = 0.0f;
  for (int i = 0; i < 2000; ++i) {
    a->process(ia.v, oa);
    b->process(ib.v, ob);
  }
  ia.v[0] = 0.0f;
  ia.v[2] = 1.0f;
  ib.v[2] = 1.0f;
  a->process(ia.v, oa);
  b->process(ib.v, ob);
  ia.v[2] = 0.0f;
  ib.v[2] = 0.0f;
  CHECK(oa[0] == ob[0]);
  for (int t = 1; t < 4800; ++t) {
    a->process(ia.v, oa);
    b->process(ib.v, ob);
    CHECK(oa[0] == ob[0]);  // identical grains: the frozen ring never changed
  }
}

TEST_CASE("grains: unfreeze crossfades from frozen content into live input") {
  auto m = make_grains();
  Ins ins;
  float outs[3] = {0.f, 0.f, 0.f};
  ins.v[0] = 0.6f;  // record DC 0.6
  for (int i = 0; i < 10000; ++i) m->process(ins.v, outs);
  ins.v[1] = 1.0f;  // freeze; tail captures 0.6
  for (int i = 0; i < 500; ++i) m->process(ins.v, outs);
  ins.v[1] = 0.0f;  // unfreeze into DC 0.8 — for only 300 samples, so the
  ins.v[0] = 0.8f;  // grain below reads INSIDE the 256-sample crossfade
  for (int i = 0; i < 300; ++i) m->process(ins.v, outs);
  ins.v[1] = 1.0f;  // freeze again to hold the seam still
  ins.v[0] = 0.0f;
  ins.v[2] = 1.0f;
  ins.v[3] = 0.0f;   // newest content
  ins.v[5] = 0.005f;  // 240-sample grain: span 240, delay 244 -> start is
                      // 75836 - 244, i.e. post-unfreeze write index 56; the
                      // grain reads indices 56..295: crossfade (56..255)
                      // then pure live (256..295).
  m->process(ins.v, outs);
  ins.v[2] = 0.0f;
  const float mid = std::cos(std::numbers::pi_v<float> / 4.0f);
  float x30 = 0.0f;
  float x230 = 0.0f;
  for (int t = 1; t < 240; ++t) {
    m->process(ins.v, outs);
    if (t == 30) x30 = outs[0] / mid;    // write index 86: a = 86/256
    if (t == 230) x230 = outs[0] / mid;  // write index 286: pure live
  }
  // A hard cut would read 0.8 at both points; the crossfade reads the blend.
  CHECK(x30 == doctest::Approx(0.6f + 0.2f * (86.0f / 256.0f)).epsilon(0.05));
  CHECK(x230 == doctest::Approx(0.8f).epsilon(0.02));
}

namespace {
std::string write_ramp_wav(const char* name, int frames, int rate) {
  auto path = std::filesystem::temp_directory_path() / name;
  std::vector<float> data(static_cast<std::size_t>(frames));
  for (int i = 0; i < frames; ++i)
    data[static_cast<std::size_t>(i)] = static_cast<float>(i) * 1e-4f;
  REQUIRE(ms::io::write_wav_f32(path.string(), data.data(),
                                static_cast<std::size_t>(frames), 1, rate));
  return path.string();
}
}  // namespace

TEST_CASE("grains: file mode granulates the file; live input is inert") {
  const std::string path = write_ramp_wav("modsynth_grains_ramp.wav", 9600, 48000);
  auto m = make_grains(48000.0, 1.0, 1.0, path.c_str());
  Ins ins;
  float outs[3] = {0.f, 0.f, 0.f};
  ins.v[0] = 1.0f;  // slam the live input: file mode must ignore it
  for (int i = 0; i < 5000; ++i) {
    m->process(ins.v, outs);
    CHECK(outs[0] == 0.0f);  // nothing recorded, no grains seeded
  }
  ins.v[0] = 0.0f;
  ins.v[2] = 1.0f;
  ins.v[3] = 0.0f;  // file start
  m->process(ins.v, outs);
  CHECK(outs[2] == 1.0f);
  ins.v[2] = 0.0f;
  // len 9600; size 0.1 -> dur 4800, span 4800 > len/4 = 2400 -> dur scaled
  // to 2400; start = 4. At t = 1200 (boxcar plateau): sample = file[1204].
  for (int t = 1; t < 2400; ++t) {
    m->process(ins.v, outs);
    if (t == 1200) {
      const float expect =
          1204.0f * 1e-4f * std::cos(std::numbers::pi_v<float> / 4.0f);
      CHECK(outs[0] == doctest::Approx(expect).epsilon(0.002));
    }
  }
}

TEST_CASE("grains: file at a different rate is resampled to engine rate") {
  const std::string path = write_ramp_wav("modsynth_grains_ramp24.wav", 4800, 24000);
  auto m = make_grains(48000.0, 1.0, 1.0, path.c_str());
  Ins ins;
  float outs[3] = {0.f, 0.f, 0.f};
  ins.v[2] = 1.0f;
  ins.v[3] = 0.0f;
  m->process(ins.v, outs);
  ins.v[2] = 0.0f;
  // Resampled length 9600 (same geometry as above); engine index i holds
  // file[i/2], so index 1204 holds file[602] = 0.0602.
  for (int t = 1; t < 2400; ++t) {
    m->process(ins.v, outs);
    if (t == 1200) {
      const float expect =
          602.0f * 1e-4f * std::cos(std::numbers::pi_v<float> / 4.0f);
      CHECK(outs[0] == doctest::Approx(expect).epsilon(0.005));
    }
  }
}

TEST_CASE("grains: missing file yields silence, not death") {
  auto m = make_grains(48000.0, 1.0, 1.0, "/nonexistent/definitely_missing.wav");
  Ins ins;
  float outs[3] = {0.f, 0.f, 0.f};
  ins.v[2] = 1.0f;
  m->process(ins.v, outs);
  ins.v[2] = 0.0f;
  for (int t = 0; t < 2000; ++t) {
    m->process(ins.v, outs);
    CHECK(outs[0] == 0.0f);
    CHECK(std::isfinite(outs[1]));
  }
}
