#include <doctest/doctest.h>
#include <cmath>
#include <numbers>
#include <vector>
#include "dsp/bandlimited_osc.h"
#include "dsp/modules.h"

using namespace ms::dsp;

namespace {
// Render n samples of a vco with fixed inputs.
std::vector<float> render(const char* wave, float freq, float v_oct, size_t n,
                          double rate = 48000.0) {
  const ModuleInfo* info = find_module("vco");
  REQUIRE(info != nullptr);
  auto m = info->create();
  Config cfg;
  cfg.push_back({"wave", ConfigValue{wave, 0, {}, false}});
  m->init(rate, cfg);
  std::vector<float> out(n);
  float ins[7] = {freq, v_oct, 0.f, 0.5f, 0.f, 0.f};
  float o[1];
  for (size_t i = 0; i < n; ++i) { m->process(ins, o); out[i] = o[0]; }
  return out;
}

// Goertzel magnitude at frequency f.
double goertzel(const std::vector<float>& x, double f, double rate) {
  const double w = 2.0 * std::numbers::pi * f / rate;
  const double c = 2.0 * std::cos(w);
  double s0 = 0, s1 = 0, s2 = 0;
  for (float v : x) { s0 = v + c * s1 - s2; s2 = s1; s1 = s0; }
  return std::sqrt(s1 * s1 + s2 * s2 - c * s1 * s2) /
         (static_cast<double>(x.size()) / 2.0);
}

// Error against the ideal Nyquist-limited Fourier series. Aliases of a
// coherent oscillator can fold onto legitimate harmonic bins, so measuring
// only non-harmonic bins misses them; a full reference waveform does not.
double bandlimited_error_db(const std::vector<float>& x, int fundamental,
                            bool triangle, double rate = 48000.0) {
  std::vector<double> reference(x.size(), 0.0);
  const int max_h = static_cast<int>(std::ceil(0.5 * rate)) / fundamental - 1;
  for (size_t i = 0; i < x.size(); ++i) {
    const double phase = std::fmod(
        static_cast<double>(i + 1) * static_cast<double>(fundamental) / rate, 1.0);
    if (triangle) {
      for (int h = 1; h <= max_h; h += 2)
        reference[i] += (8.0 / (std::numbers::pi * std::numbers::pi)) *
                        std::cos(2.0 * std::numbers::pi * h * phase) /
                        static_cast<double>(h * h);
    } else {
      for (int h = 1; h <= max_h; ++h)
        reference[i] -= (2.0 / std::numbers::pi) *
                        std::sin(2.0 * std::numbers::pi * h * phase) /
                        static_cast<double>(h);
    }
  }

  double signal = 0.0, error = 0.0;
  for (size_t i = 0; i < x.size(); ++i) {
    signal += reference[i] * reference[i];
    const double e = static_cast<double>(x[i]) - reference[i];
    error += e * e;
  }
  return 10.0 * std::log10(std::max(error / signal, 1e-20));
}
}  // namespace

TEST_CASE("sine frequency is accurate (zero crossings)") {
  auto x = render("sine", 100.0f, 0.0f, 48000);
  int crossings = 0;
  for (size_t i = 1; i < x.size(); ++i)
    if (x[i - 1] < 0.0f && x[i] >= 0.0f) ++crossings;
  CHECK(crossings == 100);
}

TEST_CASE("v_oct shifts by octaves") {
  auto x = render("sine", 100.0f, 1.0f, 48000);
  int crossings = 0;
  for (size_t i = 1; i < x.size(); ++i)
    if (x[i - 1] < 0.0f && x[i] >= 0.0f) ++crossings;
  CHECK(crossings == 200);
}

TEST_CASE("saw amplitude within convention") {
  auto x = render("saw", 110.0f, 0.0f, 4800);
  float mn = 1e9f, mx = -1e9f;
  for (float v : x) { mn = std::min(mn, v); mx = std::max(mx, v); }
  // A consistently scaled Fourier saw retains the ideal waveform's bounded
  // Gibbs overshoot instead of changing gain at every table boundary.
  CHECK(mx <= 1.2f);
  CHECK(mn >= -1.2f);
  CHECK(mx > 0.8f);
  CHECK(mn < -0.8f);
}

TEST_CASE("band-limited saw suppresses aliasing") {
  // 5000 Hz saw at 48k: harmonic 5 (25 kHz) aliases to 23 kHz.
  auto x = render("saw", 5000.0f, 0.0f, 1 << 15);
  double fundamental = goertzel(x, 5000.0, 48000.0);
  double alias = goertzel(x, 23000.0, 48000.0);
  // The selected Fourier table contains no partial at or above Nyquist.
  CHECK(20.0 * std::log10(alias / fundamental) < -60.0);
}

TEST_CASE("saw fundamental level is stable across table boundaries") {
  const auto two_harmonics = render("saw", 8000.0f, 0.0f, 48000);
  const auto one_harmonic = render("saw", 12000.0f, 0.0f, 48000);
  const double a = goertzel(two_harmonics, 8000.0, 48000.0);
  const double b = goertzel(one_harmonic, 12000.0, 48000.0);
  CHECK(a == doctest::Approx(b).epsilon(0.01));
}

TEST_CASE("saw morphs continuously when a harmonic becomes available") {
  const auto& bank = BandlimitedOsc::instance();
  constexpr float phase = 0.125f;
  const float below_boundary = bank.saw(phase, 11999.0f, 48000.0f);
  const float above_boundary = bank.saw(phase, 12001.0f, 48000.0f);
  CHECK(std::abs(below_boundary - above_boundary) < 0.001f);
}

TEST_CASE("lowest saw table does not fold harmonics above table Nyquist") {
  constexpr int n = 2048;
  // Harmonics above 1023 fold onto ordinary table bins when they are summed
  // into a 2048-point table. Bin 500 exposes that contamination directly.
  constexpr int bin = 500;
  std::vector<float> table(n);
  const auto& bank = BandlimitedOsc::instance();
  for (int i = 0; i < n; ++i)
    table[static_cast<std::size_t>(i)] =
        bank.saw(static_cast<float>(i) / static_cast<float>(n), 0.0f, 48000.0f);
  const double expected = 2.0 / (std::numbers::pi * static_cast<double>(bin));
  const double measured =
      goertzel(table, static_cast<double>(bin), static_cast<double>(n));
  CHECK(measured / expected == doctest::Approx(1.0).epsilon(0.01));
}

TEST_CASE("saw matches band-limited reference across the high register") {
  for (int f : {1000, 4000, 8000}) {
    CAPTURE(f);
    const auto x = render("saw", static_cast<float>(f), 0.0f, 48000);
    CHECK(bandlimited_error_db(x, f, false) < -60.0);
  }
}

TEST_CASE("triangle matches band-limited reference across the high register") {
  for (int f : {1000, 4000, 8000}) {
    CAPTURE(f);
    const auto x = render("tri", static_cast<float>(f), 0.0f, 48000);
    CHECK(bandlimited_error_db(x, f, true) < -70.0);
  }
}

TEST_CASE("band-limited pulse preserves width DC and signal convention") {
  const ModuleInfo* info = find_module("vco");
  REQUIRE(info != nullptr);
  for (float pw : {0.05f, 0.10f, 0.25f, 0.5f, 0.75f, 0.90f, 0.95f}) {
    CAPTURE(pw);
    auto m = info->create();
    Config cfg;
    cfg.push_back({"wave", ConfigValue{"pulse", 0, {}, false}});
    m->init(48000.0, cfg);
    float ins[7] = {1000.f, 0.f, 0.f, pw, 0.f, 0.f, 0.f};
    float out[1]{};
    double mean = 0.0;
    float lo = 1e9f, hi = -1e9f;
    for (int i = 0; i < 48000; ++i) {
      m->process(ins, out);
      REQUIRE(std::isfinite(out[0]));
      mean += out[0];
      lo = std::min(lo, out[0]);
      hi = std::max(hi, out[0]);
    }
    mean /= 48000.0;
    CHECK(mean == doctest::Approx(2.0f * pw - 1.0f).epsilon(0.01));
    // A truly band-limited step has bounded Gibbs ringing. Do not hard-clip
    // it back to nominal +/-1: that would recreate the aliases we removed.
    CHECK(lo >= -1.3f);
    CHECK(hi <= 1.3f);
    CHECK(lo < -0.5f);
    CHECK(hi > 0.5f);
  }
}

TEST_CASE("sync resets phase") {
  const ModuleInfo* info = find_module("vco");
  auto m = info->create();
  auto fresh = info->create();
  Config cfg;
  cfg.push_back({"wave", ConfigValue{"saw", 0, {}, false}});
  m->init(48000.0, cfg);
  fresh->init(48000.0, cfg);
  float ins[7] = {100.f, 0.f, 0.f, 0.5f, 0.f, 0.f};
  float o[1];
  float fresh_out[1];
  fresh->process(ins, fresh_out);
  for (int i = 0; i < 100; ++i) m->process(ins, o);
  float before = o[0];
  ins[5] = 1.0f;  // sync rising edge
  m->process(ins, o);
  CHECK(o[0] != doctest::Approx(before));
  CHECK(o[0] == doctest::Approx(fresh_out[0]).epsilon(1e-6));
}

TEST_CASE("band-limited vco survives hostile inputs and recovers") {
  const ModuleInfo* info = find_module("vco");
  REQUIRE(info != nullptr);
  auto m = info->create();
  Config cfg;
  cfg.push_back({"wave", ConfigValue{"saw", 0, {}, false}});
  m->init(48000.0, cfg);
  float hostile[7] = {NAN, NAN, NAN, NAN, NAN, NAN, NAN};
  float out[1]{};
  m->process(hostile, out);
  CHECK(std::isfinite(out[0]));

  float valid[7] = {440.0f, 0.0f, 0.0f, 0.5f, 0.0f, 0.0f, 0.0f};
  bool audible = false;
  for (int i = 0; i < 128; ++i) {
    m->process(valid, out);
    REQUIRE(std::isfinite(out[0]));
    audible = audible || std::abs(out[0]) > 0.1f;
  }
  CHECK(audible);
}

TEST_CASE("vco phase is an offset in cycles including when stopped") {
  const auto* info = find_module("vco");
  auto m = info->create();
  m->init(48000, {});
  float ins[7] = {0,0,0,0.5f,0,0,0.25f}, out[1];
  m->process(ins, out);
  CHECK(out[0] == doctest::Approx(1.0f));
  ins[6] = -0.25f;
  m->process(ins, out);
  CHECK(out[0] == doctest::Approx(-1.0f));
}
