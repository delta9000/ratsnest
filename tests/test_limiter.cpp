#include <doctest/doctest.h>
#include <cmath>
#include <numbers>
#include <algorithm>
#include <cstdint>
#include <vector>
#include "dsp/modules.h"
#include "dsp/mastering.h"
#include "thd.h"

using namespace ms::dsp;

namespace {
std::unique_ptr<Module> make_limiter(double rate = 48000.0) {
  const ModuleInfo* info = find_module("limiter");
  REQUIRE(info != nullptr);
  auto m = info->create();
  Config cfg;
  m->init(rate, cfg);
  return m;
}
}  // namespace

TEST_CASE("limiter: metadata and default parameters") {
  const ModuleInfo* info = find_module("limiter");
  REQUIRE(info != nullptr);
  CHECK(std::string(info->type) == "limiter");
  
  REQUIRE(info->inputs.size() == 4);
  CHECK(std::string(info->inputs[0].name) == "in_l");
  CHECK(info->inputs[0].def == 0.0f);
  CHECK(std::string(info->inputs[1].name) == "in_r");
  CHECK(info->inputs[1].def == 0.0f);
  CHECK(std::string(info->inputs[2].name) == "threshold");
  CHECK(info->inputs[2].def == -0.1f); // dBFS
  CHECK(std::string(info->inputs[3].name) == "release");
  CHECK(info->inputs[3].def == 0.05f); // s
  
  REQUIRE(info->outputs.size() == 4);
  CHECK(std::string(info->outputs[0].name) == "out_l");
  CHECK(std::string(info->outputs[1].name) == "out_r");
  CHECK(std::string(info->outputs[2].name) == "gain");
  CHECK(std::string(info->outputs[3].name) == "gr");
}

TEST_CASE("limiter: lookahead latency and hard ceiling") {
  auto m = make_limiter();
  // Threshold = -6 dBFS (0.501187)
  float ins[4] = {0.0f, 0.0f, -6.0f, 0.05f};
  float outs[4] = {};
  
  // 1. Measure latency
  // Feed a single massive impulse (2.0) at sample 0, then silence.
  // The output peak should be exactly clamped to threshold, and delayed by exactly 96 samples (2.0 ms lookahead).
  float thresh_linear = db_to_linear(-6.0f);
  int peak_idx = -1;
  float max_val = 0.0f;
  
  for (int i = 0; i < 200; ++i) {
    ins[0] = (i == 0) ? 2.0f : 0.0f;
    m->process(ins, outs);
    if (std::abs(outs[0]) > max_val) {
      max_val = std::abs(outs[0]);
      peak_idx = i;
    }
    // Hard ceiling: output must never exceed threshold
    CHECK(outs[0] <= doctest::Approx(thresh_linear));
  }
  
  // Latency at 48 kHz for 2.0 ms is exactly 96 samples!
  CHECK(peak_idx == 96);
  CHECK(max_val == doctest::Approx(thresh_linear));
}

TEST_CASE("limiter: lookahead peak extraction") {
  auto m = make_limiter();
  // Threshold = -6.0 dBFS. Feed an impulse of 2.0 at sample 0.
  // The impulse is output at sample 96.
  // Gain reduction should start rising immediately at sample 0 and be active before sample 96!
  float ins[4] = {0.0f, 0.0f, -6.0f, 0.05f};
  float outs[4] = {};
  
  bool gr_started_early = false;
  for (int i = 0; i < 150; ++i) {
    ins[0] = (i == 0) ? 2.0f : 0.0f;
    m->process(ins, outs);
    
    // Check if gain reduction is active (gr > 0.1) before the impulse is output at sample 96
    if (i > 5 && i < 96 && outs[3] > 0.1f) {
      gr_started_early = true;
    }
  }
  CHECK(gr_started_early);
}

TEST_CASE("limiter: stereo linking") {
  auto m = make_limiter();
  // Feed a hot signal on Left (2.0), Right is silent.
  // Both channels must experience the same gain reduction.
  float ins[4] = {2.0f, 0.0f, -6.0f, 0.05f};
  float outs[4] = {};
  
  for (int i = 0; i < 150; ++i) {
    m->process(ins, outs);
  }
  
  // Settle at steady state
  CHECK(outs[3] > 0.0f); // Gain reduction triggered
  CHECK(outs[2] < 1.0f); // Gain < 1.0
  // Left output is clamped, Right output is 0.0 (no signal), but the gain factor is identical
  CHECK(outs[2] == doctest::Approx(db_to_linear(-outs[3])));
}

TEST_CASE("limiter: silence and hostile values") {
  auto m = make_limiter();
  float ins[4] = {NAN, INFINITY, NAN, NAN};
  float outs[4] = {};
  m->process(ins, outs);
  CHECK(std::isfinite(outs[0]));
  CHECK(std::isfinite(outs[1]));
  CHECK(std::isfinite(outs[2]));
  CHECK(std::isfinite(outs[3]));
}

TEST_CASE("limiter: output never exceeds threshold on hostile transients") {
  for (double rate : {44100.0, 48000.0, 96000.0, 192000.0}) {
    for (float thr_db : {-0.1f, -6.0f, -24.0f}) {
      auto m = make_limiter(rate);
      const float ceiling = db_to_linear(thr_db) * 1.00001f;
      std::uint32_t seed = 0x9e3779b9u;
      float worst = 0.0f;
      for (int i = 0; i < static_cast<int>(rate); ++i) {
        seed ^= seed << 13; seed ^= seed >> 17; seed ^= seed << 5;
        const float u = static_cast<float>(seed) / 4294967296.0f;  // 0..1
        float x = 0.3f * (2.0f * u - 1.0f);
        if (seed % 97 == 0) x = (u - 0.5f) * 200.0f;           // lone spikes
        if ((i / 300) % 7 == 3) x = ((i / 3) % 2) ? 8.0f : -8.0f;  // hot square bursts
        if (i % 5000 < 3) x = 1e6f;                              // absurd clicks
        // Release swept from 1 ms upward: recovery right before a peak.
        float ins[4] = {x, -0.5f * x, thr_db, 0.001f + 0.01f * u};
        float outs[4];
        m->process(ins, outs);
        worst = std::max({worst, std::abs(outs[0]), std::abs(outs[1])});
      }
      CAPTURE(rate);
      CAPTURE(thr_db);
      CHECK(worst <= ceiling);
    }
  }
}

TEST_CASE("limiter: 50 Hz under 6 dB of gain reduction stays clean") {
  // The gain envelope must use the whole lookahead and hold across the
  // troughs of a bass waveform; tracking the waveform is what distorts it.
  for (float release : {0.05f, 0.2f}) {
    auto m = make_limiter();
    const float thr = db_to_linear(-6.0f);
    std::vector<float> y;
    for (int i = 0; i < 96000; ++i) {
      const float x = 2.0f * thr * std::sin(2.0f * std::numbers::pi_v<float> * 50.0f * static_cast<float>(i) / 48000.0f);
      float ins[4] = {x, x, -6.0f, release}, outs[4];
      m->process(ins, outs);
      if (i >= 48000) y.push_back(outs[0]);
    }
    CAPTURE(release);
    CHECK(thd(y, 50.0, 48000.0) < 1e-4);  // < 0.01 % (was 1.06 %)
    CHECK(*std::max_element(y.begin(), y.end()) == doctest::Approx(thr).epsilon(1e-3));
  }
}

namespace {
// The largest magnitude of `x[from..to)` reconstructed at 8x: windowed-sinc
// interpolation over +/-64 input samples (Blackman), well past the 4x
// detector's own 12 taps, so it measures what a DAC would draw.
float reconstructed_peak(const std::vector<float>& x, size_t from, size_t to) {
  constexpr int kHalf = 64;
  const double pi = std::numbers::pi;
  double peak = 0.0;
  for (size_t n = from; n < to; ++n)
    for (int p = 0; p < 8; ++p) {
      double y = 0.0;
      for (int k = -kHalf; k <= kHalf; ++k) {
        const double t = p / 8.0 - k;
        const double sinc = t == 0.0 ? 1.0 : std::sin(pi * t) / (pi * t);
        const double w = 0.42 + 0.5 * std::cos(pi * t / (kHalf + 1)) +
                         0.08 * std::cos(2 * pi * t / (kHalf + 1));
        y += x[static_cast<size_t>(static_cast<long>(n) + k)] * sinc * w;
      }
      peak = std::max(peak, std::abs(y));
    }
  return static_cast<float>(peak);
}

std::unique_ptr<Module> make_limiter_tp(bool true_peak) {
  auto m = find_module("limiter")->create();
  Config cfg;
  cfg.push_back({"true_peak", ConfigValue{true_peak ? "on" : "off", 0.0, {}, false}});
  m->init(48000.0, cfg);
  return m;
}

// A sine near fs/4 at +2.9 dB starting at phase pi/4, so at fs/4 the samples
// straddle every crest at 0.707 of it, through a -1 dBFS limiter.
std::vector<float> limit_straddling_sine(bool true_peak, double hz) {
  auto m = make_limiter_tp(true_peak);
  std::vector<float> y;
  for (int i = 0; i < 12000; ++i) {
    const double ph = 2.0 * std::numbers::pi * hz * i / 48000.0 + std::numbers::pi / 4.0;
    const float x = 1.4f * static_cast<float>(std::sin(ph));
    float ins[4] = {x, x, -1.0f, 0.05f}, outs[4];
    m->process(ins, outs);
    y.push_back(outs[0]);
  }
  return y;
}
}  // namespace

TEST_CASE("limiter: true_peak holds the ceiling between samples") {
  const float ceiling = db_to_linear(-1.0f);
  for (double hz : {12000.0, 11950.0, 11000.0}) {
    CAPTURE(hz);
    // At exactly fs/4 the samples never reach a crest: sample peaks of 0.99
    // come down only to the ceiling, so the waveform between them is 3 dB
    // over it. (Off fs/4 they drift onto the crests, which the hold catches.)
    const float off = reconstructed_peak(limit_straddling_sine(false, hz), 4000, 11000);
    if (hz == 12000.0) CHECK(linear_to_db(off / ceiling) > 2.5f);
    const float on = reconstructed_peak(limit_straddling_sine(true, hz), 4000, 11000);
    CHECK(on <= ceiling);
  }
}

TEST_CASE("limiter: true_peak delays the audio by the detector's lag") {
  for (bool tp : {false, true}) {
    auto m = make_limiter_tp(tp);
    int at = -1;
    for (int i = 0; i < 200 && at < 0; ++i) {
      float ins[4] = {i == 0 ? 0.5f : 0.0f, 0.0f, -0.1f, 0.05f}, outs[4];
      m->process(ins, outs);
      if (outs[0] != 0.0f) at = i;
    }
    CHECK(at == 96 + (tp ? TruePeak4x::kLag : 0));
  }
}
