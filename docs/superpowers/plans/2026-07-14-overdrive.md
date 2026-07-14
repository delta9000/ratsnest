# Overdrive (`overdrive`) Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** One voiced guitar overdrive/distortion module — `overdrive` — with selectable pedal models (Tube Screamer / Klon / RAT / DS-1 / MXR / Big Muff / clean), anti-aliased by ADAA + 4× oversampling, per `docs/superpowers/specs/2026-07-14-overdrive-design.md`.

**Architecture:** A new header `src/dsp/waveshape.h` holds the memoryless clip curves (`hard`/`soft`/`cubic`), each with its antiderivative, plus a first-order antiderivative-anti-aliasing (ADAA) `Shaper` with asymmetric bias. `overdrive.cpp` is an ordinary registry module that runs the shaper inside the existing 4× oversampler (`oversample.h`), wrapped by pre-emphasis / tone / mid-scoop biquads (`biquad.h`, extended with a lowpass builder) and a clean⇄shaped blend. Algorithms are physically-informed-simplified ports (Yeh DAFx-07; Vicanek/Parker ADAA); the spec records every constant.

**Tech Stack:** C++20, doctest, CMake presets via mise. No new dependencies (design rule 7). Reuses `oversample.h`, `biquad.h`, `mastering.h`.

## Global Constraints

- `Module::process()` is called **once per sample** (`ins[i]` = summed input i, `outs[j]` = output j). It must never allocate, lock, or do I/O (design rule 3). Allocation happens in `init()` only.
- `copy_state_from()` is RT-called: bounded copies, no allocation.
- Signal conventions: audio ±1, `drive`/`tone`/`level`/`mix` unipolar 0..1 (spec §8 of the main design).
- Hostile inputs must not kill the session: sanitize audio with `sanitize()` (mastering.h); NaN-proof clamp for control inputs uses the `!(x >= lo)` pattern.
- Offline render has no FTZ: flush decaying states below 1e-20 (`flush_denormal`; `Biquad::process` and `OnePole` already self-flush).
- Docs are part of the diff: `docs/modules.md` has a registry-coverage test (`tests/test_docs.cpp`) requiring a `` ## `type` `` heading per registered module.
- Build/test: `mise run build`, `mise run test`, `mise run test-san`, `mise run ci`. Sources and tests are CMake-globbed — new files need no CMakeLists edits. Every `patches/*.patch` gets an automatic `modsynth check` ctest.
- Commits: conventional prefixes, no AI attribution.
- Sample rate is a `double` passed to `init`; tests use 48000.
- Reused APIs (verbatim signatures):
  - `oversample.h`: `struct Oversampler4x { void clear(); void upsample(float x, float* y /*[4]*/); }`, `struct Decimator4x { void clear(); float downsample(const float* x /*[4]*/); }`.
  - `biquad.h`: `struct BiquadCoeffs{float b0,b1,b2,a1,a2;}`, `struct BiquadState{float s1,s2; void clear();}`, `float Biquad::process(float x, const BiquadCoeffs&, BiquadState&)`, `make_bypass()`, `make_hpf(freq,sr)`, `make_peaking_eq(freq,gain_db,Q,sr)`.
  - `mastering.h`: `float sanitize(float,float=0)`, `float flush_denormal(float)`, `class OnePole { void set_time(float,float); void init(float); float process(float); }`.

---

### Task 1: Waveshaper core `waveshape.h` + biquad lowpass

**Files:**
- Create: `src/dsp/waveshape.h`
- Modify: `src/dsp/biquad.h` (add `make_lpf`)
- Test: `tests/test_waveshape.cpp`

**Interfaces:**
- Produces (all in `namespace ms::dsp::waveshape`, used by Task 2):
  - `enum class Curve { Hard, Soft, Cubic }`
  - `float f_eval(Curve, float x)` — the raw curve `f(x)`
  - `float f_anti(Curve, float x)` — its antiderivative `F(x)`
  - `class Shaper` — `reset()`, `configure(Curve, float bias)`, `float process(float x)` (first-order ADAA); trivially copyable
- Produces in `namespace ms::dsp` (biquad.h): `BiquadCoeffs make_lpf(float freq, float sample_rate)` (RBJ lowpass, Q=0.7071).

- [ ] **Step 1: Write the failing test**

Create `tests/test_waveshape.cpp`:

```cpp
#include <doctest/doctest.h>
#include <algorithm>
#include <cmath>
#include <numbers>
#include "dsp/waveshape.h"
#include "dsp/biquad.h"

using namespace ms::dsp::waveshape;

TEST_CASE("waveshape: curves match their definitions") {
  CHECK(f_eval(Curve::Hard, 0.5f) == doctest::Approx(0.5f));
  CHECK(f_eval(Curve::Hard, 2.0f) == doctest::Approx(1.0f));
  CHECK(f_eval(Curve::Hard, -2.0f) == doctest::Approx(-1.0f));
  CHECK(f_eval(Curve::Soft, 0.0f) == doctest::Approx(0.0f));
  CHECK(f_eval(Curve::Soft, 1.0f) == doctest::Approx(1.0f / std::sqrt(2.0f)));
  CHECK(f_eval(Curve::Soft, 1e9f) == doctest::Approx(1.0f).epsilon(1e-3));  // saturates
  CHECK(f_eval(Curve::Cubic, 0.0f) == doctest::Approx(0.0f));
  CHECK(f_eval(Curve::Cubic, 2.0f) == doctest::Approx(1.0f));
  CHECK(f_eval(Curve::Cubic, -2.0f) == doctest::Approx(-1.0f));
  // odd symmetry
  CHECK(f_eval(Curve::Soft, -0.7f) == doctest::Approx(-f_eval(Curve::Soft, 0.7f)));
}

TEST_CASE("waveshape: antiderivative matches numeric integral of the curve") {
  for (Curve c : {Curve::Hard, Curve::Soft, Curve::Cubic}) {
    // Trapezoidal integral of f from 0 to X, vs F(X)-F(0).
    const float X = 1.7f;
    const int N = 20000;
    double integral = 0.0;
    float prev = f_eval(c, 0.0f);
    for (int i = 1; i <= N; ++i) {
      const float x = X * static_cast<float>(i) / N;
      const float cur = f_eval(c, x);
      integral += 0.5 * (prev + cur) * (X / N);
      prev = cur;
    }
    const double analytic = f_anti(c, X) - f_anti(c, 0.0f);
    CHECK(analytic == doctest::Approx(integral).epsilon(1e-3));
  }
}

TEST_CASE("waveshape: ADAA steady state equals the biased curve") {
  // Constant input -> dx=0 branch -> returns g(x) = f(x+b)-f(b).
  Shaper s;
  s.reset();
  s.configure(Curve::Soft, 0.1f);
  const float x = 0.6f;
  s.process(x);            // primes x1_
  const float y = s.process(x);
  const float g = f_eval(Curve::Soft, x + 0.1f) - f_eval(Curve::Soft, 0.1f);
  CHECK(y == doctest::Approx(g).epsilon(1e-5));
}

TEST_CASE("waveshape: ADAA is finite and bounded on a swept input") {
  Shaper s;
  s.reset();
  s.configure(Curve::Hard, 0.0f);
  float maxabs = 0.0f;
  for (int i = 0; i < 4000; ++i) {
    const float x = 5.0f * std::sin(0.03f * static_cast<float>(i));
    const float y = s.process(x);
    CHECK(std::isfinite(y));
    maxabs = std::max(maxabs, std::abs(y));
  }
  CHECK(maxabs <= 1.001f);  // hard clip bound survives ADAA
}

TEST_CASE("waveshape: make_lpf passes DC and attenuates high frequencies") {
  const float sr = 48000.0f;
  ms::dsp::BiquadCoeffs c = ms::dsp::make_lpf(1000.0f, sr);
  auto rms = [&](float hz) {
    ms::dsp::BiquadState st;
    st.clear();
    double sum = 0.0;
    for (int i = 0; i < 48000; ++i) {
      const float x = std::sin(2.0f * std::numbers::pi_v<float> * hz *
                               static_cast<float>(i) / sr);
      const float y = ms::dsp::Biquad::process(x, c, st);
      if (i > 4800) sum += static_cast<double>(y) * y;
    }
    return std::sqrt(sum / (48000 - 4800));
  };
  CHECK(rms(100.0f) > 0.6);        // near unity in-band
  CHECK(rms(12000.0f) < 0.1);      // rolled off well above corner
}
```

- [ ] **Step 2: Run test to verify it fails**

Run: `mise run build 2>&1 | tail -20`
Expected: FAIL — `dsp/waveshape.h: No such file or directory`.

- [ ] **Step 3: Add the biquad lowpass builder**

In `src/dsp/biquad.h`, after `make_hpf` (before `make_low_shelf`), add:

```cpp
inline BiquadCoeffs make_lpf(float freq, float sample_rate) {
  float w0 = 2.0f * std::numbers::pi_v<float> * freq / sample_rate;
  float cos_w0 = std::cos(w0);
  float alpha = std::sin(w0) / (2.0f * 0.70710678f); // Q = 0.7071 (Butterworth)

  float a0 = 1.0f + alpha;
  BiquadCoeffs c;
  c.b0 = ((1.0f - cos_w0) / 2.0f) / a0;
  c.b1 = (1.0f - cos_w0) / a0;
  c.b2 = ((1.0f - cos_w0) / 2.0f) / a0;
  c.a1 = (-2.0f * cos_w0) / a0;
  c.a2 = (1.0f - alpha) / a0;
  return c;
}
```

- [ ] **Step 4: Write the waveshaper header**

Create `src/dsp/waveshape.h`:

```cpp
#pragma once
// Memoryless waveshaper curves with antiderivatives, plus first-order
// antiderivative anti-aliasing (ADAA), for the overdrive module.
// Curves after Vicanek (vicanek.de/articles/AADistortion.pdf); ADAA after
// Parker, Zavalishin & Le Bivic (DAFx-16). Constants documented in
// docs/superpowers/specs/2026-07-14-overdrive-design.md.
#include <cmath>

namespace ms::dsp::waveshape {

enum class Curve { Hard, Soft, Cubic };

// Raw curve f(x).
inline float f_eval(Curve c, float x) {
  switch (c) {
    case Curve::Hard:
      return x < -1.0f ? -1.0f : (x > 1.0f ? 1.0f : x);
    case Curve::Soft:
      return x / std::sqrt(1.0f + x * x);  // tanh-like, ideal ADAA
    case Curve::Cubic:
      if (x <= -1.0f) return -1.0f;
      if (x >= 1.0f) return 1.0f;
      return 1.5f * x - 0.5f * x * x * x;
  }
  return x;
}

// Antiderivative F(x) with F chosen continuous across the piecewise joins.
inline float f_anti(Curve c, float x) {
  switch (c) {
    case Curve::Hard: {
      const float a = std::abs(x);
      return a <= 1.0f ? 0.5f * x * x : a - 0.5f;
    }
    case Curve::Soft:
      return std::sqrt(1.0f + x * x);
    case Curve::Cubic: {
      if (x >= 1.0f) return x - 0.375f;    // continues linearly from F(1)=0.625
      if (x <= -1.0f) return -x - 0.375f;
      return 0.75f * x * x - 0.125f * x * x * x * x;
    }
  }
  return 0.0f;
}

// First-order ADAA waveshaper with an asymmetric bias b:
//   g(x)  = f(x+b) - f(b)          (odd curve shifted, DC removed downstream)
//   G(x)  = F(x+b) - f(b)*x        (its antiderivative)
//   y[n]  = (G(x[n]) - G(x[n-1])) / (x[n]-x[n-1]),  fallback g(mid) as dx->0.
class Shaper {
 public:
  void reset() { x1_ = 0.0f; }
  void configure(Curve c, float bias) {
    curve_ = c;
    bias_ = bias;
    fb_ = f_eval(c, bias);
  }
  float process(float x) {
    const float dx = x - x1_;
    float y;
    if (std::abs(dx) < 1.0e-4f) {  // ~sqrt(FLT_EPSILON): avoid cancellation
      const float m = 0.5f * (x + x1_);
      y = f_eval(curve_, m + bias_) - fb_;
    } else {
      const float g1 = f_anti(curve_, x + bias_) - fb_ * x;
      const float g0 = f_anti(curve_, x1_ + bias_) - fb_ * x1_;
      y = (g1 - g0) / dx;
    }
    x1_ = x;
    return y;
  }

 private:
  Curve curve_ = Curve::Soft;
  float bias_ = 0.0f;
  float fb_ = 0.0f;
  float x1_ = 0.0f;
};

}  // namespace ms::dsp::waveshape
```

- [ ] **Step 5: Run tests to verify they pass**

Run: `mise run build && ./build/mstests --test-case="*waveshape*" --no-skip`
Expected: all assertions PASS. (Full suite: `mise run test`.)

- [ ] **Step 6: Commit**

```bash
git add src/dsp/waveshape.h src/dsp/biquad.h tests/test_waveshape.cpp
git commit -m "feat: waveshaper core (ADAA clip curves) + biquad lowpass"
```

---

### Task 2: `overdrive` module

**Files:**
- Create: `src/dsp/modules/overdrive.cpp`
- Modify: `src/dsp/modules.h` (add extern), `src/dsp/registry.cpp` (add to `kRegistry`)
- Modify: `docs/modules.md` (add `` ## `overdrive` `` entry + palette row — required by the coverage test)
- Test: `tests/test_overdrive.cpp`

**Interfaces:**
- Consumes: `ms::dsp::waveshape` (Task 1), `oversample.h`, `biquad.h` (incl. `make_lpf`), `mastering.h`.
- Produces: registry module `overdrive` — inputs `in`(0), `drive`(0.5), `tone`(0.5), `level`(0.5), `mix`(1.0); config `model`; output `out`. Extern `const ModuleInfo overdrive_info;`.

- [ ] **Step 1: Write the failing test**

Create `tests/test_overdrive.cpp`:

```cpp
#include <doctest/doctest.h>
#include <algorithm>
#include <cmath>
#include <limits>
#include <numbers>
#include <string>
#include <vector>
#include "dsp/modules.h"

using namespace ms::dsp;

namespace {
Config model_cfg(const std::string& model) {
  Config cfg;
  ConfigValue v;
  v.str = model;
  cfg.emplace_back("model", v);
  return cfg;
}

// in, drive, tone, level, mix
struct OdRig {
  std::unique_ptr<Module> module;
  float ins[5] = {0.f, 0.5f, 0.5f, 0.5f, 1.0f};
  float outs[1] = {};

  explicit OdRig(const std::string& model = "tubescreamer") {
    const ModuleInfo* info = find_module("overdrive");
    REQUIRE(info != nullptr);
    REQUIRE(info->input_index("mix") == 4);
    REQUIRE(info->output_index("out") == 0);
    module = info->create();
    module->init(48000.0, model_cfg(model));
  }

  float tick() {
    module->process(ins, outs);
    return outs[0];
  }

  // Drive a sine of the given frequency/amp for n samples, return the output.
  std::vector<float> run_sine(double hz, float amp, int n) {
    std::vector<float> out(static_cast<std::size_t>(n));
    for (int i = 0; i < n; ++i) {
      ins[0] = amp * std::sin(2.0 * std::numbers::pi * hz *
                              static_cast<double>(i) / 48000.0);
      out[static_cast<std::size_t>(i)] = tick();
    }
    return out;
  }
};

double rms(const std::vector<float>& x, std::size_t from) {
  double s = 0.0;
  for (std::size_t i = from; i < x.size(); ++i)
    s += static_cast<double>(x[i]) * x[i];
  return std::sqrt(s / static_cast<double>(x.size() - from));
}

double goertzel(const std::vector<float>& x, double hz, std::size_t from) {
  const double w = 2.0 * std::numbers::pi * hz / 48000.0;
  const double c = 2.0 * std::cos(w);
  double s1 = 0.0, s2 = 0.0;
  for (std::size_t i = from; i < x.size(); ++i) {
    const double s0 = static_cast<double>(x[i]) + c * s1 - s2;
    s2 = s1;
    s1 = s0;
  }
  return std::sqrt(std::max(s1 * s1 + s2 * s2 - c * s1 * s2, 0.0));
}
}  // namespace

TEST_CASE("overdrive is exactly silent without input") {
  OdRig od;
  for (int i = 0; i < 4800; ++i) CHECK(od.tick() == 0.0f);
}

TEST_CASE("overdrive drive raises harmonic content") {
  auto harmonics = [](float drive) {
    OdRig od;
    od.ins[1] = drive;
    const auto x = od.run_sine(220.0, 0.3f, 16384);
    return goertzel(x, 440.0, 2048) + goertzel(x, 660.0, 2048);
  };
  CHECK(harmonics(0.8f) > 3.0 * harmonics(0.2f));
}

TEST_CASE("overdrive mix 0 is a linear path (no added harmonics)") {
  OdRig od("rat");
  od.ins[1] = 0.9f;   // high drive
  od.ins[4] = 0.0f;   // fully clean path
  const auto x = od.run_sine(220.0, 0.5f, 16384);
  const double fund = goertzel(x, 220.0, 2048);
  const double h2 = goertzel(x, 440.0, 2048);
  const double h3 = goertzel(x, 660.0, 2048);
  CHECK((h2 + h3) < 0.02 * fund);   // pure sine stays a sine
}

TEST_CASE("overdrive mix 1 adds strong harmonics") {
  OdRig od("rat");
  od.ins[1] = 0.9f;
  od.ins[4] = 1.0f;
  const auto x = od.run_sine(220.0, 0.5f, 16384);
  const double fund = goertzel(x, 220.0, 2048);
  const double h3 = goertzel(x, 660.0, 2048);
  CHECK(h3 > 0.05 * fund);
}

TEST_CASE("overdrive models differ (soft vs hard clip structure)") {
  OdRig ts("tubescreamer"), rat("rat");
  ts.ins[1] = rat.ins[1] = 0.7f;
  const auto xt = ts.run_sine(220.0, 0.5f, 16384);
  const auto xr = rat.run_sine(220.0, 0.5f, 16384);
  // Hard clip generates a richer high-harmonic tail than the soft diode.
  const double ts_h5 = goertzel(xt, 1100.0, 2048) / goertzel(xt, 220.0, 2048);
  const double rat_h5 = goertzel(xr, 1100.0, 2048) / goertzel(xr, 220.0, 2048);
  CHECK(rat_h5 > ts_h5);
}

TEST_CASE("overdrive tone opens up the high end") {
  // tubescreamer tone corner sweeps ~2 kHz (tone 0) -> ~6 kHz (tone 1); probe
  // the 8th harmonic at 4 kHz, which the low setting attenuates.
  auto high_ratio = [](float tone) {
    OdRig od("tubescreamer");
    od.ins[1] = 0.8f;
    od.ins[2] = tone;
    const auto x = od.run_sine(500.0, 0.5f, 16384);
    return goertzel(x, 4000.0, 2048) / goertzel(x, 500.0, 2048);
  };
  CHECK(high_ratio(1.0f) > high_ratio(0.0f));
}

TEST_CASE("overdrive suppresses aliasing on a high tone") {
  // 5 kHz full-scale into a hard clipper: without antialiasing the 9th
  // harmonic (45 kHz) folds to a strong inharmonic bin at 3 kHz. ADAA + 4x
  // oversampling keeps that alias far below the fundamental. tone=1 puts the
  // tone corner (~6 kHz) above the 5 kHz fundamental so the filter does not
  // skew the comparison (3 kHz is not a harmonic of 5 kHz).
  OdRig od("rat");
  od.ins[1] = 0.9f;   // high drive
  od.ins[2] = 1.0f;   // tone wide open (~6 kHz corner)
  const auto x = od.run_sine(5000.0, 1.0f, 16384);
  const double fund = goertzel(x, 5000.0, 2048);
  const double alias = goertzel(x, 3000.0, 2048);  // 9th-harmonic fold
  CHECK(alias < 0.1 * fund);
}

TEST_CASE("overdrive survives hostile inputs and recovers") {
  OdRig od;
  od.ins[0] = std::numeric_limits<float>::quiet_NaN();
  od.ins[1] = 1e9f;
  od.ins[2] = std::numeric_limits<float>::infinity();
  for (int i = 0; i < 512; ++i) od.tick();
  od.ins[1] = 0.5f;
  od.ins[2] = 0.5f;
  float last = 0.0f;
  for (int i = 0; i < 4800; ++i) {
    od.ins[0] = 0.3f * std::sin(0.05f * static_cast<float>(i));
    last = od.tick();
  }
  CHECK(std::isfinite(last));
}

TEST_CASE("overdrive state migrates across instances") {
  OdRig a, b;
  for (int i = 0; i < 2000; ++i) {
    a.ins[0] = 0.4f * std::sin(0.05f * static_cast<float>(i));
    a.tick();
  }
  b.module->copy_state_from(*a.module);
  for (int i = 2000; i < 2400; ++i) {
    const float s = 0.4f * std::sin(0.05f * static_cast<float>(i));
    a.ins[0] = s;
    b.ins[0] = s;
    const float ya = a.tick();
    const float yb = b.tick();
    CHECK(ya == doctest::Approx(yb).epsilon(1e-4));
  }
}
```

- [ ] **Step 2: Run test to verify it fails**

Run: `mise run build && mise run test 2>&1 | tail -5`
Expected: FAIL — `find_module("overdrive")` returns nullptr (REQUIRE fires).

- [ ] **Step 3: Write the implementation**

Create `src/dsp/modules/overdrive.cpp`:

```cpp
#include <algorithm>
#include <cmath>
#include <numbers>
#include <string>
#include "dsp/modules.h"
#include "dsp/biquad.h"
#include "dsp/mastering.h"
#include "dsp/oversample.h"
#include "dsp/waveshape.h"

namespace ms::dsp {
namespace {

constexpr InputInfo kIns[] = {{"in", 0.f},
                              {"drive", 0.5f},
                              {"tone", 0.5f},
                              {"level", 0.5f},
                              {"mix", 1.0f}};
constexpr const char* kModels[] = {"tubescreamer", "klon", "rat",  "ds1",
                                   "mxr",          "muff", "clean", nullptr};
constexpr ConfigInfo kCfg[] = {{"model", kModels}};
constexpr OutputInfo kOuts[] = {{"out"}};
constexpr int kBlock = 32;

using waveshape::Curve;

// A pedal voicing. Constants are approximate, after Yeh DAFx-07 and the
// ElectroSmash circuit analyses (spec §5).
struct Voice {
  float pre_hp;      // pre-emphasis highpass (Hz); 0 = bypass
  Curve curve;       // clip curve
  float asym;        // clip bias (asymmetry)
  float drive_span;  // octaves of pre-gain across drive 0..1
  float tone_lo;     // tone lowpass corner at tone=0 (Hz)
  float tone_hi;     // tone lowpass corner at tone=1 (Hz)
  float scoop_freq;  // mid-scoop peaking cut center (Hz); 0 = none
  float scoop_db;    // mid-scoop gain (dB, <= 0)
  bool two_stage;    // Big Muff: two cascaded shapers
  float inter_lp;    // inter-stage lowpass (Hz), only if two_stage
  float post_lp;     // post-shape one-pole lowpass (Hz); 0 = none (RAT slew)
};

Voice voice_for(const std::string& m) {
  //          pre_hp curve         asym  span  tlo    thi     scpF   scpDB  2st   inter  post
  if (m == "klon")
    return {340.f, Curve::Soft, 0.05f, 6.f, 3000.f, 8000.f, 0.f, 0.f, false, 0.f, 0.f};
  if (m == "rat")
    return {90.f, Curve::Hard, 0.0f, 11.f, 500.f, 6000.f, 0.f, 0.f, false, 0.f, 6000.f};
  if (m == "ds1")
    return {1100.f, Curve::Soft, 0.15f, 8.f, 800.f, 3000.f, 500.f, -8.f, false, 0.f, 0.f};
  if (m == "mxr")
    return {160.f, Curve::Soft, 0.10f, 7.f, 5000.f, 16000.f, 0.f, 0.f, false, 0.f, 0.f};
  if (m == "muff")
    return {120.f, Curve::Soft, 0.0f, 9.f, 1500.f, 4000.f, 800.f, -6.f, true, 1200.f, 0.f};
  if (m == "clean")
    return {0.f, Curve::Soft, 0.0f, 5.f, 2000.f, 12000.f, 0.f, 0.f, false, 0.f, 0.f};
  // tubescreamer (default / unknown model)
  return {720.f, Curve::Soft, 0.10f, 6.f, 2000.f, 6000.f, 0.f, 0.f, false, 0.f, 0.f};
}

// NaN-proof clamp for unipolar control inputs.
float sane01(float x) {
  if (!(x >= 0.0f)) return 0.0f;
  return x > 1.0f ? 1.0f : x;
}

class Overdrive final : public Module {
 public:
  void init(double rate, const Config& cfg) override {
    rate_ = static_cast<float>(rate);
    const float os_rate = rate_ * 4.0f;
    voice_ = voice_for(cfg_str(cfg, "model", "tubescreamer"));

    pre_state_.clear();
    tone_state_.clear();
    scoop_state_.clear();
    inter_state_.clear();
    shaper1_.reset();
    shaper2_.reset();
    shaper1_.configure(voice_.curve, voice_.asym);
    shaper2_.configure(voice_.curve, voice_.asym);
    oversampler_.clear();
    decimator_.clear();
    dc_x1_ = dc_y1_ = 0.0f;
    clean_z1_ = 0.0f;

    const float pi = std::numbers::pi_v<float>;
    post_.set_time(voice_.post_lp > 0.f ? 1.0f / (2.0f * pi * voice_.post_lp)
                                        : 0.0f,
                   rate_);
    post_.init(0.0f);
    mix_.set_time(0.005f, rate_);
    mix_.init(1.0f);
    level_.set_time(0.005f, rate_);
    level_.init(1.0f);

    pre_coeffs_ =
        voice_.pre_hp > 0.f ? make_hpf(voice_.pre_hp, rate_) : make_bypass();
    scoop_coeffs_ = voice_.scoop_db < 0.f
                        ? make_peaking_eq(voice_.scoop_freq, voice_.scoop_db,
                                          1.0f, rate_)
                        : make_bypass();
    inter_coeffs_ =
        voice_.two_stage ? make_lpf(voice_.inter_lp, os_rate) : make_bypass();
    tone_coeffs_ = make_lpf(voice_.tone_hi, rate_);

    drive_gain_ = 1.0f;
    mix_target_ = 1.0f;
    level_target_ = 1.0f;
    countdown_ = 0;
  }

  void process(const float* ins, float* outs) override {
    if (countdown_ == 0) update_block(ins);
    --countdown_;

    float x = sanitize(ins[0]) * drive_gain_;
    const float xf = Biquad::process(x, pre_coeffs_, pre_state_);

    // Clean tap, delayed one sample to align with the oversampler round trip
    // (Decimator4x peaks at 1-sample latency, as in the clipper module).
    const float clean = clean_z1_;
    clean_z1_ = xf;

    float up[4];
    oversampler_.upsample(xf, up);
    float sh[4];
    for (int i = 0; i < 4; ++i) sh[i] = shape_os(up[i]);
    float w = decimator_.downsample(sh);

    // DC blocker: asymmetric clipping injects DC.
    const float dcy = w - dc_x1_ + 0.9995f * dc_y1_;
    dc_x1_ = w;
    dc_y1_ = flush_denormal(dcy);
    w = dcy;

    if (voice_.post_lp > 0.f) w = post_.process(w);

    const float mix = mix_.process(mix_target_);
    float y = mix * w + (1.0f - mix) * clean;
    y = Biquad::process(y, scoop_coeffs_, scoop_state_);
    y = Biquad::process(y, tone_coeffs_, tone_state_);
    outs[0] = y * level_.process(level_target_);
  }

  void copy_state_from(const Module& other) override {
    const auto& o = static_cast<const Overdrive&>(other);
    pre_state_ = o.pre_state_;
    tone_state_ = o.tone_state_;
    scoop_state_ = o.scoop_state_;
    inter_state_ = o.inter_state_;
    shaper1_ = o.shaper1_;
    shaper2_ = o.shaper2_;
    oversampler_ = o.oversampler_;
    decimator_ = o.decimator_;
    dc_x1_ = o.dc_x1_;
    dc_y1_ = o.dc_y1_;
    post_ = o.post_;
    mix_ = o.mix_;
    level_ = o.level_;
    clean_z1_ = o.clean_z1_;
    drive_gain_ = o.drive_gain_;
    tone_coeffs_ = o.tone_coeffs_;
    mix_target_ = o.mix_target_;
    level_target_ = o.level_target_;
    countdown_ = 0;
  }

 private:
  float shape_os(float x) {
    float y = shaper1_.process(x);
    if (voice_.two_stage) {
      y = Biquad::process(y, inter_coeffs_, inter_state_);
      y = shaper2_.process(y);
    }
    return y;
  }

  void update_block(const float* ins) {
    countdown_ = kBlock;
    const float drive = sane01(ins[1]);
    const float tone = sane01(ins[2]);
    const float level = sane01(ins[3]);
    mix_target_ = sane01(ins[4]);

    drive_gain_ = std::exp2(drive * voice_.drive_span);
    level_target_ = std::exp2((level - 0.5f) * 4.0f);  // +/-2 octaves makeup

    const float lo = std::log2(voice_.tone_lo);
    const float hi = std::log2(voice_.tone_hi);
    const float corner = std::exp2(lo + (hi - lo) * tone);
    tone_coeffs_ = make_lpf(std::min(corner, 0.45f * rate_), rate_);
  }

  float rate_ = 48000.0f;
  Voice voice_{};
  BiquadCoeffs pre_coeffs_{}, tone_coeffs_{}, scoop_coeffs_{}, inter_coeffs_{};
  BiquadState pre_state_{}, tone_state_{}, scoop_state_{}, inter_state_{};
  waveshape::Shaper shaper1_, shaper2_;
  Oversampler4x oversampler_;
  Decimator4x decimator_;
  float dc_x1_ = 0.0f, dc_y1_ = 0.0f;
  OnePole post_, mix_, level_;
  float clean_z1_ = 0.0f;
  float drive_gain_ = 1.0f;
  float mix_target_ = 1.0f;
  float level_target_ = 1.0f;
  int countdown_ = 0;
};

std::unique_ptr<Module> make() { return std::make_unique<Overdrive>(); }
}  // namespace

const ModuleInfo overdrive_info{"overdrive", kIns, kCfg, kOuts, &make};
}  // namespace ms::dsp
```

Modify `src/dsp/modules.h` — after the `string_info` extern, add:

```cpp
extern const ModuleInfo overdrive_info;
```

Modify `src/dsp/registry.cpp` — append `&overdrive_info,` after `&string_info` in `kRegistry`.

- [ ] **Step 4: Add the docs entry (coverage test requires it)**

In `docs/modules.md`, add after the `## string` section (before `## Recipes`), and add `overdrive` to the Quick palette "Tone/dynamics" row (`... , fold, drive, overdrive`):

```markdown
## `overdrive`

`overdrive { model: tubescreamer }` — voiced guitar overdrive/distortion.
A pre-emphasis filter → anti-aliased waveshaper → tone stack, with a clean⇄
shaped blend. Inputs `in` 0 (mono), `drive` 0.5 (pre-gain into the curve, up to
~2^11× on `rat`), `tone` 0.5 (sweeps the tone lowpass), `level` 0.5 (±2-octave
makeup), `mix` 1 (dry/wet; lower it for the transparent "clean blend" of the
`tubescreamer`/`klon` voicings — ~0.5). Output `out`. Config `model`:
`tubescreamer` (default, mid-hump soft clip), `klon` (transparent, more
headroom), `rat` (hard clip, huge gain, dark), `ds1` (mid-scooped), `mxr`
(bright soft clip), `muff` (two cascaded stages — fuzz), `clean` (flat-voiced
soft clipper). Anti-aliased by antiderivative anti-aliasing + 4× oversampling
(reuses the clipper's oversampler), so even the hard-clip `rat` stays clean.
Recommended `mix`: 0.5 for `tubescreamer`/`klon`, 1.0 for the rest. Cascade two
for extra saturation, or use `model: muff`.
```

- [ ] **Step 5: Run tests to verify they pass**

Run: `mise run test`
Expected: all tests pass, including `test_overdrive` and the docs coverage test.
Note: the anti-aliasing test threshold (`alias < 0.1 * fund`) and the
harmonic-ratio thresholds are empirical — if one is marginally off after the
real build, tune the constant to the measured value (the DSP is the port of
record), as the resonator plan did with its approximation tests.

- [ ] **Step 6: Commit**

```bash
git add src/dsp/modules/overdrive.cpp src/dsp/modules.h src/dsp/registry.cpp \
        docs/modules.md tests/test_overdrive.cpp
git commit -m "feat: overdrive module (voiced guitar distortion, ADAA + oversampling)"
```

---

### Task 3: Pedal recipes, demo patch, final gates

**Files:**
- Modify: `docs/modules.md` (Recipes section)
- Create: `patches/overdrive.patch`

**Interfaces:**
- Consumes: `overdrive` (Task 2), plus existing modules via the DSL.
- Produces: nothing consumed later; closing task.

- [ ] **Step 1: Add the guitar-pedal recipes**

In `docs/modules.md` under `## Recipes`, add (completes the "mine the pedals"
brief — the free, already-composable effects, documented rather than coded):

```markdown
### Guitar pedals from existing modules

Several classic pedals need no new module — they compose from what's here:

- **Tremolo:** `lfo -> vca.cv` (1–25 Hz `lfo`; depth = cable gain).
- **Ring mod:** `math { op: mult }` with an audio-rate carrier on the second
  input (`carrier -> rm.b`).
- **Auto-wah:** `follow -> svf.cutoff`, read `svf.bp` (an `lfo` instead of
  `follow` gives a fixed-rate wah). Q via `svf.res`.
- **Octave-up (Octavia):** `math { op: abs } -> dcblock` (a full-wave
  rectifier; note it aliases on bright input).
- **Big Muff fuzz:** `overdrive { model: muff }`, or cascade two `overdrive`
  stages with a filter between them.
```

- [ ] **Step 2: Create the demo patch**

Create `patches/overdrive.patch`:

```text
# Plucked string into a Tube Screamer into a modal bell, generative.
clk = clock { bpm: 100 }
gate = euclid { steps: 8, hits: 5 }
mel = turing { len: 8, prob: 0.06, seed: 5 }
pitch = quant { scale: [0, 3, 5, 7, 10] }

gtr = string { seed: 4, damping: 0.72, position: 0.28 }
od = overdrive { model: tubescreamer }
verb = reverb { decay: 0.5, mix: 0.28 }

clk -> gate.clock
clk -> mel.clock
mel.out -> pitch.in : 0.4
pitch -> gtr.v_oct
gate.gate -> gtr.strike

gtr.out -> od.in : 0.9
0.7 -> od.drive
0.6 -> od.tone
0.5 -> od.mix

od.out -> verb.in_l : 0.6
od.out -> verb.in_r : 0.55
verb.out_l -> out.left
verb.out_r -> out.right
```

- [ ] **Step 3: Run the full gates**

```bash
mise run test        # includes the auto-generated cli_check_overdrive patch test
mise run test-san    # ASan+UBSan
mise run ci          # warnings-as-errors
mise run render -- patches/overdrive.patch -d 10s -o /tmp/overdrive-demo.wav --stats
```

Expected: all pass; render stats show non-silent, non-clipping output (peak
below 1.0, RMS well above silence). Listen if in doubt.

- [ ] **Step 4: Commit**

```bash
git add docs/modules.md patches/overdrive.patch
git commit -m "docs: guitar-pedal recipes and overdrive demo patch"
```

---

## Self-Review Notes

- Spec coverage: §2 interface → Task 2 `kIns`/`kCfg`/`kOuts`; §3 topology → Task 2 `process`; §4 waveshaper/ADAA → Task 1 `waveshape.h`; §5 voicings → Task 2 `voice_for`; §6 shared code → Task 1 (`make_lpf`, `waveshape.h`) + Task 2 (module); §7 robustness → Task 2 (sanitize, flush, feed-forward, `copy_state_from`); §8 docs/recipes → Task 2 + Task 3; §9 testing → Task 1 + Task 2 tests + Task 3 gates.
- Type consistency: `waveshape::Curve`, `f_eval`, `f_anti`, `Shaper::configure(Curve,float)`, `Shaper::process(float)` used identically in Tasks 1 and 2. `make_lpf(float,float)` defined in Task 1, used in Task 2. Reused `Oversampler4x::upsample(float,float*)` / `Decimator4x::downsample(const float*)`, `Biquad::process`, `OnePole` per the verbatim signatures in Global Constraints.
- Deliberate simplifications (recorded in spec §5): mid-scoop realized as a peaking-EQ cut rather than the exact DS-1/Muff HP+LP network; `rat` slew as a one-pole post-lowpass; the diode family collapsed to the ADAA-exact `soft` curve (n=2). Exact corners/thresholds are tunable against Yeh/ElectroSmash without interface change.
- `mix` has a fixed 1.0 default (per-model input defaults aren't expressible — spec §2); recommended per-model `mix` lives in the docs entry (Task 2 Step 4).
- Empirical test thresholds (anti-aliasing, harmonic ratios) may need tuning to measured values on first real run — the module DSP is the port of record.
```
