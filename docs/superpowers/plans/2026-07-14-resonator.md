# Resonator (`modal` + `string`) Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Two physical-resonator modules — `modal` (parallel bandpass mode bank) and `string` (extended Karplus-Strong) — sharing a header-only DSP core, per `docs/superpowers/specs/2026-07-14-resonator-design.md`.

**Architecture:** A new header `src/dsp/resonate.h` holds the shared primitives (Simper SVF with fast/accurate tuning, cosine-series generator, delay line with Hermite read + allpass, 3-tap damping FIR, DC blocker, xorshift RNG, Plucker, and the Rings mapping curves). `modal.cpp` and `string.cpp` are ordinary registry modules built on it. Algorithms are ports of Mutable Instruments Rings (MIT), verified against source; the spec records every constant.

**Tech Stack:** C++20, doctest, CMake presets via mise. No new dependencies (design rule 7).

## Global Constraints

- `Module::process()` is called **once per sample** (`ins[i]` = summed input i, `outs[j]` = output j). It must never allocate, lock, or do I/O (design rule 3). Allocation happens in `init()` only.
- `copy_state_from()` is RT-called: bounded copies, no allocation. Vectors may differ in size across reload — copy `min(size)` prefixes (see `delay.cpp:90`).
- Signal conventions: audio ±1, unipolar 0..1, v_oct in octaves, gates rise through 0.5 (spec §4 of the main design).
- Hostile inputs must not kill the session: NaN-proof clamps use the `!(x >= lo)` pattern (`delay.cpp:40`); sanitize audio entering feedback structures (`!(|x| < 1e6) -> 0`).
- Offline render has no FTZ: flush decaying states below 1e-20 (block-rate flush is acceptable; see spec §6).
- Docs are part of the diff: `docs/modules.md` has a registry-coverage test (`tests/test_docs.cpp`) requiring a `` ## `type` `` heading per registered module.
- Build/test: `mise run build`, `mise run test`, `mise run test-san`, `mise run ci`. Sources and tests are CMake-globbed — new files need no CMakeLists edits. Every `patches/*.patch` gets an automatic `modsynth check` ctest.
- Commits: conventional prefixes, no AI attribution.
- Sample rate is a `double` passed to `init`; tests use 48000.

---

### Task 1: Shared DSP core `resonate.h`

**Files:**
- Create: `src/dsp/resonate.h`
- Test: `tests/test_resonate.cpp`

**Interfaces:**
- Produces (all in `namespace ms::dsp::resonate`, used by Tasks 2–3):
  - `float semitones_to_ratio(float x)` — `2^(x/12)`
  - `float stiffness_curve(float g)` — modal structure→stiffness
  - `float dispersion_from_structure(float s)` — string structure→bipolar dispersion
  - `class Svf` — `reset()`, `set_f_q_fast(f,q)`, `set_f_q_accurate(f,q)`, `float process_bp(float)`, `float process_lp(float)`, `flush_denormals()`; trivially copyable
  - `class CosineSeries` — `explicit CosineSeries(float x)`, `float next()` (first call returns 1.0)
  - `class DelayLine` — `init(size_t min_size)`, `write(float)`, `float read(size_t)`, `float read_linear(float)`, `float read_hermite(float)`, `float allpass(float in, size_t delay, float coeff)`, `copy_from(const DelayLine&)` (RT-safe), `clear()`
  - `class DampingFir` — `reset()`, `configure(float gain, float brightness, int n)`, `float process(float)`
  - `class DcBlocker` — `init(float pole)`, `float process(float)`
  - `class Rng` — `explicit Rng(uint32_t seed)`, `seed(uint32_t)`, `float bipolar()`
  - `class Plucker` — `init(uint32_t seed)` (allocates), `trigger(float f, float cutoff, float position)`, `float process()`, `copy_from(const Plucker&)` (RT-safe)

- [ ] **Step 1: Write the failing test**

Create `tests/test_resonate.cpp`:

```cpp
#include <doctest/doctest.h>
#include <cmath>
#include <numbers>
#include <vector>
#include "dsp/resonate.h"

using namespace ms::dsp::resonate;

TEST_CASE("semitones_to_ratio matches 2^(x/12)") {
  CHECK(semitones_to_ratio(12.0f) == doctest::Approx(2.0f));
  CHECK(semitones_to_ratio(-12.0f) == doctest::Approx(0.5f));
  CHECK(semitones_to_ratio(7.01955f) == doctest::Approx(1.5f).epsilon(1e-4));
}

TEST_CASE("stiffness curve endpoints and plateau") {
  CHECK(stiffness_curve(0.0f) == doctest::Approx(-0.0625f));
  CHECK(stiffness_curve(0.25f) == doctest::Approx(0.0f));
  CHECK(stiffness_curve(0.29f) == doctest::Approx(0.0f));
  CHECK(stiffness_curve(0.9f) == doctest::Approx(1.0f).epsilon(0.02));
  CHECK(stiffness_curve(1.0f) == doctest::Approx(2.0f).epsilon(0.02));
  // Monotone rise across the exponential segment.
  CHECK(stiffness_curve(0.6f) > stiffness_curve(0.4f));
}

TEST_CASE("dispersion mapping is bipolar with a dead zone") {
  CHECK(dispersion_from_structure(0.0f) == doctest::Approx(-0.99984f));
  CHECK(dispersion_from_structure(0.24f) == doctest::Approx(0.0f));
  CHECK(dispersion_from_structure(0.25f) == doctest::Approx(0.0f));
  CHECK(dispersion_from_structure(0.26f) == doctest::Approx(0.0f));
  CHECK(dispersion_from_structure(0.9995f) ==
        doctest::Approx(0.9993f).epsilon(1e-3));
}

TEST_CASE("fast tan approximation tracks tan(pi f) in the audio band") {
  for (float hz = 16.0f; hz < 16000.0f; hz *= 1.5f) {
    const float f = hz / 48000.0f;
    const float exact = std::tan(std::numbers::pi_v<float> * f);
    CHECK(Svf::tan_fast(f) == doctest::Approx(exact).epsilon(0.01));
  }
}

TEST_CASE("svf bandpass selects its center frequency") {
  auto rms_at = [](float tone_hz) {
    Svf svf;
    svf.reset();
    svf.set_f_q_fast(440.0f / 48000.0f, 20.0f);
    double sum = 0.0;
    for (int i = 0; i < 48000; ++i) {
      const float x = std::sin(2.0f * std::numbers::pi_v<float> * tone_hz *
                               static_cast<float>(i) / 48000.0f);
      const float y = svf.process_bp(x);
      if (i > 4800) sum += static_cast<double>(y) * y;
    }
    return std::sqrt(sum / (48000 - 4800));
  };
  CHECK(rms_at(440.0f) > 4.0 * rms_at(1760.0f));
}

TEST_CASE("cosine series: first weight is 1, exact at special positions") {
  CosineSeries at_zero(0.0f);
  for (int n = 0; n < 8; ++n)
    CHECK(at_zero.next() == doctest::Approx(1.0f).epsilon(1e-3));

  // position 0.5: w_n = 0.5 + 0.5*cos(pi n) -> 1, 0, 1, 0, ...
  CosineSeries at_half(0.5f);
  CHECK(at_half.next() == doctest::Approx(1.0f));
  CHECK(at_half.next() == doctest::Approx(0.0f).epsilon(1e-4));
  CHECK(at_half.next() == doctest::Approx(1.0f).epsilon(1e-4));

  // Approximate elsewhere: within 0.15 of the true series for n <= 8.
  CosineSeries approx(0.1f);
  for (int n = 0; n < 8; ++n) {
    const float exact =
        0.5f + 0.5f * std::cos(2.0f * std::numbers::pi_v<float> * 0.1f *
                               static_cast<float>(n));
    CHECK(std::abs(approx.next() - exact) < 0.15f);
  }
}

TEST_CASE("delay line reads back written history") {
  DelayLine d;
  d.init(64);
  for (int i = 0; i < 100; ++i) d.write(static_cast<float>(i));
  // read(k) is the sample written k writes ago (read(0) = most recent).
  CHECK(d.read(0) == doctest::Approx(99.0f));
  CHECK(d.read(10) == doctest::Approx(89.0f));
  CHECK(d.read_linear(10.5f) == doctest::Approx(88.5f));
  CHECK(d.read_hermite(10.0f) == doctest::Approx(89.0f).epsilon(1e-4));
}

TEST_CASE("damping fir at brightness 1 gain 1 is a pure one-sample delay") {
  DampingFir fir;
  fir.reset();
  fir.configure(1.0f, 1.0f, 1);  // reach targets after one sample
  float prev = 0.0f;
  for (int i = 1; i <= 16; ++i) {
    const float x = static_cast<float>(i);
    const float y = fir.process(x);
    if (i > 1) CHECK(y == doctest::Approx(prev));
    prev = x;
  }
}

TEST_CASE("dc blocker removes a constant input") {
  DcBlocker dc;
  dc.init(1.0f - 10.0f / 48000.0f);
  float y = 1.0f;
  for (int i = 0; i < 48000; ++i) y = dc.process(1.0f);
  CHECK(std::abs(y) < 1e-3f);
}

TEST_CASE("plucker is seed-deterministic") {
  auto run = [](uint32_t seed) {
    Plucker p;
    p.init(seed);
    p.trigger(220.0f / 48000.0f, 0.1f, 0.3f);
    std::vector<float> out(512);
    for (auto& s : out) s = p.process();
    return out;
  };
  const auto a = run(7), b = run(7), c = run(8);
  CHECK(a == b);
  CHECK(a != c);
  // The burst actually contains energy.
  double sum = 0.0;
  for (float s : a) sum += static_cast<double>(s) * s;
  CHECK(sum > 0.0);
}
```

- [ ] **Step 2: Run test to verify it fails**

Run: `mise run build 2>&1 | tail -20`
Expected: FAIL — `dsp/resonate.h: No such file or directory`

- [ ] **Step 3: Write the implementation**

Create `src/dsp/resonate.h`:

```cpp
#pragma once
// Shared physical-resonator DSP core for the modal/string modules.
// Algorithms derived from Mutable Instruments Rings (MIT,
// github.com/pichenettes/eurorack); constants documented in
// docs/superpowers/specs/2026-07-14-resonator-design.md.
#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <numbers>
#include <vector>

namespace ms::dsp::resonate {

inline float semitones_to_ratio(float x) {
  return std::exp2(x * (1.0f / 12.0f));
}

// Rings' lut_stiffness, evaluated directly. structure in 0..1.
inline float stiffness_curve(float g) {
  g = std::clamp(g, 0.0f, 0.9995f);
  if (g < 0.25f) return (g - 0.25f) * 0.25f;
  if (g < 0.3f) return 0.0f;
  if (g < 0.9f)
    return 0.01f * (std::pow(10.0f, (g - 0.3f) / 0.6f * 2.005f) - 1.0f);
  const float t = (g - 0.9f) * 10.0f;
  return 1.5f - 0.5f * std::cos(t * t * std::numbers::pi_v<float>);
}

// Rings' string-model structure fold: bipolar dispersion, dead zone at
// 0.24..0.26 (pure Karplus-Strong).
inline float dispersion_from_structure(float s) {
  s = std::clamp(s, 0.0f, 0.9995f);
  if (s < 0.24f) return (s - 0.24f) * 4.166f;
  if (s > 0.26f) return (s - 0.26f) * 1.35135f;
  return 0.0f;
}

// Simper trapezoidal state-variable filter (stmlib::Svf).
class Svf {
 public:
  void reset() { s1_ = s2_ = bp_ = lp_ = 0.0f; }
  // 5th-order odd polynomial tan() fit, accurate 16 Hz..16 kHz at 48 kHz
  // (Rings FREQUENCY_FAST). Monotone and finite over 0..0.499, so hostile
  // cutoffs stay stable even where the fit is loose.
  static float tan_fast(float f) {
    constexpr float pi = std::numbers::pi_v<float>;
    constexpr float a = 3.260e-01f * pi * pi * pi;
    constexpr float b = 1.823e-01f * pi * pi * pi * pi * pi;
    const float f2 = f * f;
    return f * (pi + f2 * (a + b * f2));
  }
  void set_f_q_fast(float f, float q) { set_g_q(tan_fast(f), q); }
  void set_f_q_accurate(float f, float q) {
    set_g_q(std::tan(std::numbers::pi_v<float> * f), q);
  }
  float process_bp(float in) {
    tick(in);
    return bp_;
  }
  float process_lp(float in) {
    tick(in);
    return lp_;
  }
  void flush_denormals() {
    if (std::abs(s1_) < 1e-20f) s1_ = 0.0f;
    if (std::abs(s2_) < 1e-20f) s2_ = 0.0f;
  }

 private:
  void set_g_q(float g, float q) {
    g_ = g;
    r_ = 1.0f / q;
    h_ = 1.0f / (1.0f + r_ * g_ + g_ * g_);
  }
  void tick(float in) {
    const float hp = (in - r_ * s1_ - g_ * s1_ - s2_) * h_;
    bp_ = g_ * hp + s1_;
    s1_ = g_ * hp + bp_;
    lp_ = g_ * bp_ + s2_;
    s2_ = g_ * bp_ + lp_;
  }
  float g_ = 0.0f, r_ = 1.0f, h_ = 0.5f;
  float s1_ = 0.0f, s2_ = 0.0f, bp_ = 0.0f, lp_ = 0.0f;
};

// Generates w_n = 0.5 + 0.5*cos(2*pi*n*x) for n = 0, 1, 2, ... via a
// two-multiply resonator recurrence; first next() returns exactly 1.
// The coefficient uses Rings' parabolic approximation of 2*cos(2*pi*x)
// (exact at x = 0, 0.25, 0.5).
class CosineSeries {
 public:
  explicit CosineSeries(float x) {
    float sign = 16.0f;
    x -= 0.25f;
    if (x < 0.0f) {
      x = -x;
    } else if (x > 0.5f) {
      x -= 0.5f;
    } else {
      sign = -16.0f;
    }
    c_ = sign * x * (1.0f - 2.0f * x);
    y0_ = 0.5f;
    y1_ = c_ * 0.25f;
  }
  float next() {
    const float t = y0_;
    y0_ = c_ * y0_ - y1_;
    y1_ = t;
    return t + 0.5f;
  }

 private:
  float c_, y0_, y1_;
};

// Power-of-two ring buffer. write() then read(k) returns the sample
// written k writes ago (read(0) = the sample just written), matching
// stmlib::DelayLine so the Rings loop structure ports one-to-one.
class DelayLine {
 public:
  void init(std::size_t min_size) {
    std::size_t size = 1;
    while (size < min_size) size <<= 1;
    buf_.assign(size, 0.0f);
    mask_ = size - 1;
    write_ = 0;
  }
  void clear() { std::fill(buf_.begin(), buf_.end(), 0.0f); }
  void write(float s) {
    write_ = (write_ - 1) & mask_;
    buf_[write_] = s;
  }
  float read(std::size_t delay) const { return buf_[(write_ + delay) & mask_]; }
  float read_linear(float delay) const {
    const auto di = static_cast<std::size_t>(delay);
    const float f = delay - static_cast<float>(di);
    return buf_[(write_ + di) & mask_] * (1.0f - f) +
           buf_[(write_ + di + 1) & mask_] * f;
  }
  float read_hermite(float delay) const {
    const auto di = static_cast<std::size_t>(delay);
    const float f = delay - static_cast<float>(di);
    const std::size_t t = write_ + di;
    const float xm1 = buf_[(t - 1) & mask_];
    const float x0 = buf_[t & mask_];
    const float x1 = buf_[(t + 1) & mask_];
    const float x2 = buf_[(t + 2) & mask_];
    const float c = (x1 - xm1) * 0.5f;
    const float v = x0 - x1;
    const float w = c + v;
    const float a = w + v + (x2 - x0) * 0.5f;
    const float b = w + a;
    return (((a * f - b) * f + c) * f + x0);
  }
  // First-order allpass over a tap of this line (stmlib DelayLine::Allpass).
  float allpass(float in, std::size_t delay, float coefficient) {
    const float r = read(delay);
    const float w = in + coefficient * r;
    write(w);
    return r - coefficient * w;
  }
  // RT-safe bounded copy for hot-reload state migration.
  void copy_from(const DelayLine& o) {
    const std::size_t n = std::min(buf_.size(), o.buf_.size());
    std::copy_n(o.buf_.begin(), n, buf_.begin());
    write_ = o.write_ & mask_;
  }

 private:
  std::vector<float> buf_;
  std::size_t mask_ = 0;
  std::size_t write_ = 0;
};

// 3-tap symmetric FIR lowpass with linear per-sample coefficient
// interpolation: y = gain * (h0*x[n-1] + h1*(x[n] + x[n-2])),
// h0 = (1+b)/2, h1 = (1-b)/4 (Rings DampingFilter).
class DampingFir {
 public:
  void reset() {
    x1_ = x2_ = 0.0f;
    gh0_ = gh1_ = gh0_step_ = gh1_step_ = 0.0f;
  }
  void configure(float gain, float brightness, int n) {
    const float h0 = (1.0f + brightness) * 0.5f;
    const float h1 = (1.0f - brightness) * 0.25f;
    const float inv = 1.0f / static_cast<float>(n);
    gh0_step_ = (gain * h0 - gh0_) * inv;
    gh1_step_ = (gain * h1 - gh1_) * inv;
  }
  float process(float x) {
    gh0_ += gh0_step_;
    gh1_ += gh1_step_;
    const float y = gh0_ * x1_ + gh1_ * (x + x2_);
    x2_ = x1_;
    x1_ = x;
    return y;
  }

 private:
  float gh0_ = 0.0f, gh1_ = 0.0f, gh0_step_ = 0.0f, gh1_step_ = 0.0f;
  float x1_ = 0.0f, x2_ = 0.0f;
};

// One-pole DC blocker: y = x - x' + pole*y'.
class DcBlocker {
 public:
  void init(float pole) {
    pole_ = pole;
    x_ = y_ = 0.0f;
  }
  float process(float in) {
    y_ = in - x_ + pole_ * y_;
    x_ = in;
    if (std::abs(y_) < 1e-20f) y_ = 0.0f;
    return y_;
  }

 private:
  float pole_ = 0.995f, x_ = 0.0f, y_ = 0.0f;
};

// xorshift32; zero state is forbidden.
class Rng {
 public:
  explicit Rng(uint32_t seed = 1u) : s_(seed ? seed : 1u) {}
  void seed(uint32_t s) { s_ = s ? s : 1u; }
  float bipolar() {
    s_ ^= s_ << 13;
    s_ ^= s_ >> 17;
    s_ ^= s_ << 5;
    return static_cast<float>(s_) * (2.0f / 4294967296.0f) - 1.0f;
  }

 private:
  uint32_t s_;
};

// Noise-burst pluck exciter (Rings Plucker): a period-scaled white-noise
// burst through a feedback comb and a lowpass. f/cutoff normalized (f/fs).
class Plucker {
 public:
  void init(uint32_t seed) {
    comb_.init(256);
    svf_.reset();
    rng_.seed(seed);
    remaining_ = 0;
    comb_period_ = 0.0f;
    comb_gain_ = 0.0f;
  }
  void trigger(float f, float cutoff, float position) {
    const float ratio = position * 0.9f + 0.05f;
    float period = ratio / f;
    remaining_ = static_cast<int>(period);
    while (period >= 255.0f) period *= 0.5f;
    comb_period_ = period;
    comb_gain_ = (1.0f - position) * 0.8f;
    svf_.set_f_q_fast(std::min(cutoff, 0.499f), 1.0f);
  }
  float process() {
    float in = 0.0f;
    if (remaining_ > 0) {
      in = rng_.bipolar();
      --remaining_;
    }
    const float s = in + comb_gain_ * comb_.read_linear(comb_period_);
    comb_.write(s);
    return svf_.process_lp(s);
  }
  void copy_from(const Plucker& o) {
    comb_.copy_from(o.comb_);
    svf_ = o.svf_;
    rng_ = o.rng_;
    remaining_ = o.remaining_;
    comb_period_ = o.comb_period_;
    comb_gain_ = o.comb_gain_;
  }

 private:
  DelayLine comb_;
  Svf svf_;
  Rng rng_{1u};
  int remaining_ = 0;
  float comb_period_ = 0.0f;
  float comb_gain_ = 0.0f;
};

}  // namespace ms::dsp::resonate
```

- [ ] **Step 4: Run tests to verify they pass**

Run: `mise run build && ./build/mstests --test-case="*resonate*" --no-skip` — or simply `mise run test` (the binary is `build/mstests`; ctest runs it as the `unit` test).
Expected: all assertions PASS.

(To make the new cases easy to filter, prefix each TEST_CASE name above with `resonate: ` — e.g. `TEST_CASE("resonate: stiffness curve endpoints and plateau")`.)

- [ ] **Step 5: Commit**

```bash
git add src/dsp/resonate.h tests/test_resonate.cpp
git commit -m "feat: shared resonator DSP core (SVF, cosine series, delay, plucker)"
```

---

### Task 2: `modal` module

**Files:**
- Create: `src/dsp/modules/modal.cpp`
- Modify: `src/dsp/modules.h` (add extern), `src/dsp/registry.cpp` (add to `kRegistry`)
- Modify: `docs/modules.md` (add `` ## `modal` `` entry — required by the coverage test)
- Test: `tests/test_modal.cpp`

**Interfaces:**
- Consumes: everything in `ms::dsp::resonate` (Task 1 signatures).
- Produces: registry module `modal` — inputs `in`(0), `freq`(220), `v_oct`(0), `strike`(0), `structure`(0.25), `brightness`(0.5), `damping`(0.5), `position`(0.25); config `modes`; outputs `out`, `odd`, `even`. Extern symbol `const ModuleInfo modal_info;`.

- [ ] **Step 1: Write the failing test**

Create `tests/test_modal.cpp`:

```cpp
#include <doctest/doctest.h>
#include <cmath>
#include <limits>
#include <numbers>
#include <utility>
#include <vector>
#include "dsp/modules.h"

using namespace ms::dsp;

namespace {
// in, freq, v_oct, strike, structure, brightness, damping, position
struct ModalRig {
  std::unique_ptr<Module> module;
  float ins[8] = {0.f, 220.f, 0.f, 0.f, 0.25f, 0.5f, 0.5f, 0.25f};
  float outs[3] = {};

  explicit ModalRig(const Config& cfg = {}) {
    const ModuleInfo* info = find_module("modal");
    REQUIRE(info != nullptr);
    REQUIRE(info->input_index("strike") == 3);
    REQUIRE(info->output_index("even") == 2);
    module = info->create();
    module->init(48000.0, cfg);
  }

  float tick() {
    module->process(ins, outs);
    return outs[0];
  }

  // Strike once, then collect n samples of free decay.
  std::vector<float> pluck_and_collect(int n) {
    ins[3] = 1.0f;
    std::vector<float> out(static_cast<std::size_t>(n));
    out[0] = tick();
    ins[3] = 0.0f;
    for (int i = 1; i < n; ++i) out[static_cast<std::size_t>(i)] = tick();
    return out;
  }
};

double rms(const std::vector<float>& x, std::size_t from, std::size_t to) {
  double s = 0.0;
  for (std::size_t i = from; i < to; ++i)
    s += static_cast<double>(x[i]) * x[i];
  return std::sqrt(s / static_cast<double>(to - from));
}

double goertzel(const std::vector<float>& x, double hz) {
  const double w = 2.0 * std::numbers::pi * hz / 48000.0;
  const double c = 2.0 * std::cos(w);
  double s1 = 0.0, s2 = 0.0;
  for (float v : x) {
    const double s0 = static_cast<double>(v) + c * s1 - s2;
    s2 = s1;
    s1 = s0;
  }
  return std::sqrt(std::max(s1 * s1 + s2 * s2 - c * s1 * s2, 0.0));
}
}  // namespace

TEST_CASE("modal is exactly silent without excitation") {
  ModalRig m;
  for (int i = 0; i < 4800; ++i) CHECK(m.tick() == 0.0f);
}

TEST_CASE("modal strike rings and decays") {
  ModalRig m;
  const auto x = m.pluck_and_collect(48000);
  CHECK(rms(x, 0, 4800) > 1e-3);
  CHECK(rms(x, 43200, 48000) < rms(x, 0, 4800));
}

TEST_CASE("modal damping extends decay time") {
  auto tail = [](float damping) {
    ModalRig m;
    m.ins[6] = damping;
    const auto x = m.pluck_and_collect(48000);
    return rms(x, 24000, 48000);
  };
  CHECK(tail(0.75f) > 2.0 * tail(0.25f));
}

TEST_CASE("modal rings at its harmonic partials on the plateau") {
  ModalRig m;  // structure 0.25: stiffness 0, harmonic series
  const auto x = m.pluck_and_collect(16384);
  // Fundamental and 2nd partial well above an off-mode frequency.
  CHECK(goertzel(x, 220.0) > 5.0 * goertzel(x, 311.0));
  CHECK(goertzel(x, 440.0) > 5.0 * goertzel(x, 311.0));
}

TEST_CASE("modal v_oct doubles the fundamental") {
  ModalRig m;
  m.ins[2] = 1.0f;
  const auto x = m.pluck_and_collect(16384);
  CHECK(goertzel(x, 440.0) > 5.0 * goertzel(x, 220.0));
}

TEST_CASE("modal position 0.5 nulls the 2nd partial but not the 1st") {
  auto partial2 = [](float pos) {
    ModalRig m;
    m.ins[7] = pos;
    const auto x = m.pluck_and_collect(16384);
    return std::pair{goertzel(x, 220.0), goertzel(x, 440.0)};
  };
  const auto [f1_half, f2_half] = partial2(0.5f);
  const auto [f1_q, f2_q] = partial2(0.25f);
  CHECK(f2_half < 0.05 * f2_q);       // 2nd partial nulled at center strike
  CHECK(f1_half > 0.25 * f1_q);       // fundamental survives
}

TEST_CASE("modal out is the sum of odd and even outputs") {
  ModalRig m;
  m.ins[3] = 1.0f;
  for (int i = 0; i < 1000; ++i) {
    m.module->process(m.ins, m.outs);
    m.ins[3] = 0.0f;
    CHECK(m.outs[0] ==
          doctest::Approx(m.outs[1] + m.outs[2]).epsilon(1e-4));
  }
}

TEST_CASE("modal survives hostile inputs and recovers") {
  ModalRig m;
  m.ins[3] = 1.0f;
  m.tick();
  m.ins[3] = 0.0f;
  m.ins[0] = std::numeric_limits<float>::quiet_NaN();
  m.ins[1] = std::numeric_limits<float>::infinity();
  m.ins[4] = -1e9f;
  for (int i = 0; i < 256; ++i) m.tick();
  m.ins[0] = 0.0f;
  m.ins[1] = 220.0f;
  m.ins[4] = 0.25f;
  float last = 0.0f;
  for (int i = 0; i < 4800; ++i) last = m.tick();
  CHECK(std::isfinite(last));
}

TEST_CASE("modal state migrates across instances") {
  ModalRig a, b;
  a.ins[3] = 1.0f;
  a.tick();
  a.ins[3] = 0.0f;
  for (int i = 0; i < 2000; ++i) a.tick();
  b.module->copy_state_from(*a.module);
  for (int i = 0; i < 256; ++i) {
    const float ya = a.tick();
    const float yb = b.tick();
    CHECK(ya == doctest::Approx(yb).epsilon(1e-4));
  }
}

TEST_CASE("modal modes config rounds to even and caps the bank") {
  Config cfg;
  ConfigValue v;
  v.num = 7.0;
  v.is_num = true;
  cfg.emplace_back("modes", v);
  ModalRig m(cfg);  // 7 -> 8 modes; just verify it runs and rings
  const auto x = m.pluck_and_collect(4800);
  CHECK(rms(x, 0, 4800) > 1e-4);
}

TEST_CASE("modal migration across a modes-config change keeps ringing") {
  // Spec §6: changing `modes:` migrates the overlapping prefix of the bank.
  ModalRig a;  // 64 modes
  Config cfg;
  ConfigValue v;
  v.num = 32.0;
  v.is_num = true;
  cfg.emplace_back("modes", v);
  ModalRig b(cfg);
  a.ins[3] = 1.0f;
  a.tick();
  a.ins[3] = 0.0f;
  for (int i = 0; i < 2000; ++i) a.tick();
  b.module->copy_state_from(*a.module);
  double sum = 0.0;
  for (int i = 0; i < 4800; ++i) {
    const float y = b.tick();
    CHECK(std::isfinite(y));
    sum += static_cast<double>(y) * y;
  }
  CHECK(std::sqrt(sum / 4800.0) > 1e-5);  // the tail carried over
}
```

- [ ] **Step 2: Run test to verify it fails**

Run: `mise run build && mise run test 2>&1 | tail -5`
Expected: FAIL — `find_module("modal")` returns nullptr (REQUIRE fires), or link error until the module exists.

- [ ] **Step 3: Write the implementation**

Create `src/dsp/modules/modal.cpp`:

```cpp
#include <algorithm>
#include <cmath>
#include <vector>
#include "dsp/modules.h"
#include "dsp/resonate.h"

namespace ms::dsp {
namespace {

constexpr InputInfo kIns[] = {{"in", 0.f},          {"freq", 220.f},
                              {"v_oct", 0.f},       {"strike", 0.f},
                              {"structure", 0.25f}, {"brightness", 0.5f},
                              {"damping", 0.5f},    {"position", 0.25f}};
constexpr ConfigInfo kCfgs[] = {{"modes", nullptr}};
constexpr OutputInfo kOuts[] = {{"out"}, {"odd"}, {"even"}};
// Coefficients refresh at block rate, Rings-style (24-sample blocks there).
constexpr int kBlock = 32;

// NaN-proof clamp: !(x >= lo) also catches NaN (delay.cpp precedent).
float sane(float x, float lo, float hi) {
  if (!(x >= lo)) return lo;
  return x > hi ? hi : x;
}

class Modal final : public Module {
 public:
  void init(double rate, const Config& cfg) override {
    rate_ = static_cast<float>(rate);
    int m = static_cast<int>(cfg_num(cfg, "modes", 64.0));
    m = std::clamp(m, 2, 128);
    m += m & 1;  // Rings requires an even resolution.
    modes_.assign(static_cast<std::size_t>(m), resonate::Svf{});
    exciter_.reset();
    num_active_ = 0;
    countdown_ = 0;
    position_ = 0.25f;
    position_step_ = 0.0f;
    fc_ = 0.01f;
    prev_strike_ = 0.0f;
  }

  void process(const float* ins, float* outs) override {
    if (countdown_ == 0) update_block(ins);
    --countdown_;

    const bool strike = ins[3] > 0.5f && prev_strike_ <= 0.5f;
    prev_strike_ = ins[3];

    // The bank is a feedback-free but long-ringing structure; one hostile
    // sample would ring as NaN forever.
    float exc = ins[0];
    if (!(exc > -1e6f && exc < 1e6f)) exc = 0.0f;
    // Filtered impulse, energy-normalized against the exciter cutoff
    // (part.cc: 0.25 * 2^(fc^2 * 24 / 12) / fc).
    if (strike)
      exc += 0.25f * resonate::semitones_to_ratio(fc_ * fc_ * 24.0f) / fc_;
    const float input = exciter_.process_lp(exc) * 0.125f;

    position_ += position_step_;
    resonate::CosineSeries amps(position_);
    float odd = 0.0f;
    float even = 0.0f;
    for (int i = 0; i < num_active_; i += 2) {
      odd += amps.next() *
             modes_[static_cast<std::size_t>(i)].process_bp(input);
      even += amps.next() *
              modes_[static_cast<std::size_t>(i + 1)].process_bp(input);
    }
    outs[0] = odd + even;
    outs[1] = odd;
    outs[2] = even;
  }

  void copy_state_from(const Module& other) override {
    const auto& o = static_cast<const Modal&>(other);
    const std::size_t n = std::min(modes_.size(), o.modes_.size());
    std::copy_n(o.modes_.begin(), n, modes_.begin());
    exciter_ = o.exciter_;
    position_ = o.position_;
    position_step_ = o.position_step_;
    fc_ = o.fc_;
    prev_strike_ = o.prev_strike_;
    num_active_ = std::min(o.num_active_, static_cast<int>(modes_.size()));
    countdown_ = 0;  // recompute coefficients for this instance's bank size
  }

 private:
  void update_block(const float* ins) {
    countdown_ = kBlock;
    const float freq = sane(ins[1], 0.0f, 24000.0f);
    const float voct = sane(ins[2], -10.0f, 10.0f);
    float f = freq * std::exp2(voct) / rate_;
    f = sane(f, 1e-4f, 0.25f);

    const float structure = sane(ins[4], 0.0f, 0.9995f);
    const float b_knob = sane(ins[5], 0.0f, 1.0f);
    const float damping = sane(ins[6], 0.0f, 1.0f);
    const float pos = sane(ins[7], 0.0f, 1.0f);

    // Mode placement and Q ladder (resonator.cc ComputeFilters).
    float stiffness = resonate::stiffness_curve(structure);
    float harmonic = f;
    float stretch = 1.0f;
    // 500 * 10^(4*damping): four decades of Q.
    float q = 500.0f * std::exp2(damping * 4.0f * 3.3219281f);
    float ba = 1.0f - structure;  // brightness attenuation (1-structure)^8
    ba *= ba;
    ba *= ba;
    ba *= ba;
    float b = b_knob * b_knob;  // Rings squares brightness at the part level
    b *= 1.0f - 0.2f * ba;
    float q_loss = b * (2.0f - b) * 0.85f + 0.15f;
    const float q_loss_rate = structure * (2.0f - structure) * 0.1f;
    int num = 0;
    const int m = static_cast<int>(modes_.size());
    for (int i = 0; i < m; ++i) {
      float pf = harmonic * stretch;
      if (pf >= 0.49f) {
        pf = 0.49f;
      } else {
        num = i + 1;
      }
      auto& mode = modes_[static_cast<std::size_t>(i)];
      mode.set_f_q_fast(pf, 1.0f + pf * q);
      mode.flush_denormals();
      stretch += stiffness;
      stiffness *= stiffness < 0.0f ? 0.93f : 0.98f;
      q_loss += q_loss_rate * (1.0f - q_loss);
      harmonic += f;
      q *= q_loss;
    }
    num_active_ = std::min(num + (num & 1), m);

    // Exciter lowpass: cutoff tracks pitch, +/-48 semitones on brightness
    // (unsquared knob), Q 1.5 (part.cc internal-exciter mapping).
    const float c = b_knob * (2.0f - b_knob);
    fc_ = std::clamp(f * resonate::semitones_to_ratio((c - 0.5f) * 96.0f),
                     1e-5f, 0.499f);
    exciter_.set_f_q_fast(fc_, 1.5f);
    exciter_.flush_denormals();

    position_step_ = (pos - position_) * (1.0f / kBlock);
  }

  float rate_ = 48000.0f;
  std::vector<resonate::Svf> modes_;
  resonate::Svf exciter_;
  int num_active_ = 0;
  int countdown_ = 0;
  float position_ = 0.25f;
  float position_step_ = 0.0f;
  float fc_ = 0.01f;
  float prev_strike_ = 0.0f;
};

std::unique_ptr<Module> make() { return std::make_unique<Modal>(); }
}  // namespace

const ModuleInfo modal_info{"modal", kIns, kCfgs, kOuts, &make};
}  // namespace ms::dsp
```

Modify `src/dsp/modules.h` — after the `grains_info` extern, add:

```cpp
extern const ModuleInfo modal_info;
```

Modify `src/dsp/registry.cpp` — append to `kRegistry` (inside the braces, after `&grains_info`):

```cpp
    &modal_info,
```

- [ ] **Step 4: Add the docs entry (coverage test requires it)**

In `docs/modules.md`, add after the `## grains` section (keeping the Recipes section last), and add `modal` to the Quick palette "Sources" row (`... , sampler, modal`):

```markdown
## `modal`

`modal { modes: 64 }` — physical resonator: a parallel bank of up to 128
bandpass modes on a stretched-harmonic series (Rings' modal model). Inputs
`in` 0 (excitation audio through a brightness-tracked lowpass), `freq` 220 Hz,
`v_oct`, rising-edge `strike` (internal mallet: a filtered, energy-normalized
impulse), `structure` 0.25 (partial spacing: below 0.25 compressed/drum-like,
0.25..0.3 perfectly harmonic, then stretching toward bell/bar at 1),
`brightness` 0.5 (spectral tilt of decay and exciter color), `damping` 0.5
(decay time, exponential across ~4 decades of Q), `position` 0.25
(strike-position comb; 0.5 nulls the even partials, the fundamental always
sounds). Outputs `out` plus `odd`/`even` partial groups (`out = odd + even`).
Config `modes` 2..128, rounded to even — the voice-quality/CPU knob. Bowed
and blown textures come from `noise -> vca -> m.in` with slow `vca.cv`.
```

- [ ] **Step 5: Run tests to verify they pass**

Run: `mise run test`
Expected: all tests pass, including `test_modal` cases and the docs coverage test.

- [ ] **Step 6: Commit**

```bash
git add src/dsp/modules/modal.cpp src/dsp/modules.h src/dsp/registry.cpp \
        docs/modules.md tests/test_modal.cpp
git commit -m "feat: modal resonator module (Rings modal model)"
```

---

### Task 3: `string` module

**Files:**
- Create: `src/dsp/modules/string.cpp`
- Modify: `src/dsp/modules.h`, `src/dsp/registry.cpp`
- Modify: `docs/modules.md` (add `` ## `string` `` entry, palette row)
- Test: `tests/test_string.cpp`

**Interfaces:**
- Consumes: `ms::dsp::resonate` (Task 1).
- Produces: registry module `string` — inputs `in`(0), `freq`(220), `v_oct`(0), `strike`(0), `structure`(0.25), `brightness`(0.5), `damping`(0.5), `position`(0.25); config `seed`; outputs `out`, `aux`. Extern `const ModuleInfo string_info;`.

- [ ] **Step 1: Write the failing test**

Create `tests/test_string.cpp`:

```cpp
#include <doctest/doctest.h>
#include <cmath>
#include <limits>
#include <utility>
#include <vector>
#include "dsp/modules.h"

using namespace ms::dsp;

namespace {
Config seed_cfg(double seed) {
  Config cfg;
  ConfigValue v;
  v.num = seed;
  v.is_num = true;
  cfg.emplace_back("seed", v);
  return cfg;
}

// in, freq, v_oct, strike, structure, brightness, damping, position
struct StringRig {
  std::unique_ptr<Module> module;
  float ins[8] = {0.f, 220.f, 0.f, 0.f, 0.25f, 0.7f, 0.6f, 0.25f};
  float outs[2] = {};

  explicit StringRig(double seed = 1.0) {
    const ModuleInfo* info = find_module("string");
    REQUIRE(info != nullptr);
    REQUIRE(info->output_index("aux") == 1);
    module = info->create();
    module->init(48000.0, seed_cfg(seed));
  }

  float tick() {
    module->process(ins, outs);
    return outs[0];
  }

  std::vector<float> pluck_and_collect(int n) {
    ins[3] = 1.0f;
    std::vector<float> out(static_cast<std::size_t>(n));
    out[0] = tick();
    ins[3] = 0.0f;
    for (int i = 1; i < n; ++i) out[static_cast<std::size_t>(i)] = tick();
    return out;
  }
};

double rms(const std::vector<float>& x, std::size_t from, std::size_t to) {
  double s = 0.0;
  for (std::size_t i = from; i < to; ++i)
    s += static_cast<double>(x[i]) * x[i];
  return std::sqrt(s / static_cast<double>(to - from));
}

// Lag of the strongest normalized autocorrelation peak in [lo, hi].
int autocorr_peak(const std::vector<float>& x, int lo, int hi) {
  int best = lo;
  double best_v = -1.0;
  for (int lag = lo; lag <= hi; ++lag) {
    double s = 0.0;
    for (std::size_t i = static_cast<std::size_t>(lag); i < x.size(); ++i)
      s += static_cast<double>(x[i]) * x[i - static_cast<std::size_t>(lag)];
    if (s > best_v) {
      best_v = s;
      best = lag;
    }
  }
  return best;
}
}  // namespace

TEST_CASE("string is exactly silent without excitation") {
  StringRig s;
  for (int i = 0; i < 4800; ++i) CHECK(s.tick() == 0.0f);
}

TEST_CASE("string pluck rings at the requested pitch") {
  StringRig s;
  auto x = s.pluck_and_collect(10192);
  x.erase(x.begin(), x.begin() + 2000);  // drop the attack transient
  // 48000 / 220 = 218.2 samples.
  const int lag = autocorr_peak(x, 150, 300);
  CHECK(lag >= 216);
  CHECK(lag <= 221);
}

TEST_CASE("string v_oct halves the period") {
  StringRig s;
  s.ins[2] = 1.0f;
  auto x = s.pluck_and_collect(10192);
  x.erase(x.begin(), x.begin() + 2000);
  const int lag = autocorr_peak(x, 80, 160);
  CHECK(lag >= 107);
  CHECK(lag <= 111);
}

TEST_CASE("string damping orders decay time; damping 1 sustains") {
  auto tail = [](float damping) {
    StringRig s;
    s.ins[6] = damping;
    const auto x = s.pluck_and_collect(96000);
    return std::pair{rms(x, 24000, 48000), rms(x, 72000, 96000)};
  };
  const auto [mid_lo, late_lo] = tail(0.3f);
  const auto [mid_hi, late_hi] = tail(0.8f);
  CHECK(mid_hi > 2.0 * mid_lo);
  const auto [mid_inf, late_inf] = tail(1.0f);
  CHECK(late_inf > 0.8 * mid_inf);  // within ~1 dB: infinite sustain
}

TEST_CASE("string is seed-deterministic") {
  StringRig a(7.0), b(7.0), c(8.0);
  const auto xa = a.pluck_and_collect(4096);
  const auto xb = b.pluck_and_collect(4096);
  const auto xc = c.pluck_and_collect(4096);
  CHECK(xa == xb);
  CHECK(xa != xc);
}

TEST_CASE("string structure dead zone is pure Karplus-Strong") {
  StringRig a(3.0), b(3.0), c(3.0);
  a.ins[4] = 0.245f;
  b.ins[4] = 0.255f;
  c.ins[4] = 0.6f;
  const auto xa = a.pluck_and_collect(8192);
  const auto xb = b.pluck_and_collect(8192);
  const auto xc = c.pluck_and_collect(8192);
  CHECK(xa == xb);   // both in the 0.24..0.26 dead zone
  CHECK(xa != xc);   // dispersion audibly engaged outside it
}

TEST_CASE("string survives hostile inputs and recovers") {
  StringRig s;
  s.ins[3] = 1.0f;
  s.tick();
  s.ins[3] = 0.0f;
  s.ins[0] = std::numeric_limits<float>::quiet_NaN();
  s.ins[1] = -std::numeric_limits<float>::infinity();
  s.ins[7] = 1e18f;
  for (int i = 0; i < 512; ++i) s.tick();
  s.ins[0] = 0.0f;
  s.ins[1] = 220.0f;
  s.ins[7] = 0.25f;
  bool finite = true;
  float last = 0.0f;
  for (int i = 0; i < 48000; ++i) last = s.tick();
  for (int i = 0; i < 256; ++i) finite = finite && std::isfinite(s.tick());
  CHECK(finite);
  (void)last;
}

TEST_CASE("string state migrates across instances") {
  StringRig a(5.0), b(5.0);
  a.ins[3] = 1.0f;
  a.tick();
  a.ins[3] = 0.0f;
  for (int i = 0; i < 3000; ++i) a.tick();
  b.module->copy_state_from(*a.module);
  for (int i = 0; i < 256; ++i) {
    const float ya = a.tick();
    const float yb = b.tick();
    CHECK(ya == doctest::Approx(yb).epsilon(1e-4));
  }
}
```

- [ ] **Step 2: Run test to verify it fails**

Run: `mise run build && mise run test 2>&1 | tail -5`
Expected: FAIL — `find_module("string")` returns nullptr.

- [ ] **Step 3: Write the implementation**

Create `src/dsp/modules/string.cpp`:

```cpp
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <numbers>
#include "dsp/modules.h"
#include "dsp/resonate.h"

namespace ms::dsp {
namespace {

constexpr InputInfo kIns[] = {{"in", 0.f},          {"freq", 220.f},
                              {"v_oct", 0.f},       {"strike", 0.f},
                              {"structure", 0.25f}, {"brightness", 0.5f},
                              {"damping", 0.5f},    {"position", 0.25f}};
constexpr ConfigInfo kCfgs[] = {{"seed", nullptr}};
constexpr OutputInfo kOuts[] = {{"out"}, {"aux"}};
constexpr int kBlock = 32;
// 8192-sample line: reaches ~5.9 Hz at 48 kHz, so Rings' sub-11.7 Hz
// linear upsampler is not needed. 4 guard samples for the Hermite read.
constexpr float kMaxDelay = 8188.0f;

float sane(float x, float lo, float hi) {
  if (!(x >= lo)) return lo;
  return x > hi ? hi : x;
}

class StringModule final : public Module {
 public:
  void init(double rate, const Config& cfg) override {
    rate_ = static_cast<float>(rate);
    line_.init(8192);
    stretch_.init(4096);  // holds up to 0.475 * kMaxDelay allpass samples
    fir_.reset();
    iir_.reset();
    exciter_.reset();
    const auto seed = static_cast<uint32_t>(cfg_num(cfg, "seed", 1.0));
    plucker_.init(seed);
    noise_rng_.seed(seed ^ 0x9e3779b9u);
    exc_dc_.init(1.0f - 10.0f / rate_);
    bridge_dc_.init(1.0f - 20.0f / rate_);
    delay_ = -1.0f;  // sentinel: snap interpolators on the first block
    delay_step_ = 0.0f;
    delay_target_ = 218.0f;
    position_ = 0.25f;
    position_step_ = 0.0f;
    dispersion_ = 0.0f;
    dispersion_step_ = 0.0f;
    comp_ = 1.0f;
    comp_step_ = 0.0f;
    f_ = 220.0f / rate_;
    exc_fc_ = 0.05f;
    noise_filter_ = 0.0f;
    pos_raw_ = 0.25f;
    dispersion_noise_ = 0.0f;
    curved_bridge_ = 0.0f;
    prev_strike_ = 0.0f;
    countdown_ = 0;
  }

  void process(const float* ins, float* outs) override {
    if (countdown_ == 0) update_block(ins);
    --countdown_;

    const bool strike = ins[3] > 0.5f && prev_strike_ <= 0.5f;
    prev_strike_ = ins[3];
    if (strike) {
      // Effective f0 from the clamped delay: hostile freq cannot request a
      // multi-minute noise burst.
      plucker_.trigger(1.0f / delay_target_, 8.0f * exc_fc_, pos_raw_);
    }

    // Per-sample interpolation of block-rate targets (zipper control).
    delay_ += delay_step_;
    position_ += position_step_;
    dispersion_ += dispersion_step_;
    comp_ += comp_step_;

    // Pickup tap uses the raw (uncompensated) delay, as in Rings.
    const float comb_delay =
        std::clamp(delay_ * position_, 0.0f, kMaxDelay);

    float delay = delay_ * comp_ - 1.0f;  // IIR + FIR phase compensation
    float s;
    {
      // Dispersion path runs unconditionally; at dispersion == 0 every term
      // vanishes and it reduces to a plain Hermite read (the RNG still
      // advances, keeping seeds comparable across structure values).
      const float d = dispersion_;
      float noise = noise_rng_.bipolar();
      noise *= 1.0f / (0.2f + noise_filter_);
      dispersion_noise_ += noise_filter_ * (noise - dispersion_noise_);

      const float stretch_point =
          d <= 0.0f ? 0.0f : d * (2.0f - d) * 0.475f;
      float noise_amount = d > 0.75f ? 4.0f * (d - 0.75f) : 0.0f;
      noise_amount = noise_amount * noise_amount * 0.025f;
      const float ac_amount = d < 0.0f ? -d : 0.0f;
      const float bridge_curving = ac_amount * ac_amount * 0.01f;
      const float ap_gain = -0.618f * d / (0.15f + std::abs(d));

      float delay_fm = 1.0f;
      delay_fm += dispersion_noise_ * noise_amount;
      delay_fm -= curved_bridge_ * bridge_curving;
      delay *= delay_fm;
      if (!(delay >= 4.0f)) delay = 4.0f;
      if (delay > kMaxDelay) delay = kMaxDelay;

      const float ap_delay = delay * stretch_point;
      const float main_delay = delay - ap_delay;
      if (ap_delay >= 4.0f && main_delay >= 4.0f) {
        s = line_.read_hermite(main_delay);
        s = stretch_.allpass(s, static_cast<std::size_t>(ap_delay), ap_gain);
      } else {
        s = line_.read_hermite(delay);
      }
      const float s_ac = bridge_dc_.process(s);
      s += ac_amount * (s_ac - s);
      // Curved-bridge nonlinearity: dead-zoned asymmetric rectifier whose
      // output FMs the delay on the next sample (gong/sitar behavior).
      const float value = std::abs(s) - 0.025f;
      const float sign = s > 0.0f ? 1.0f : -1.5f;
      curved_bridge_ = (std::abs(value) + value) * sign;
    }

    // Excitation: external audio (headroom-scaled, brightness lowpass)
    // plus the pluck burst, DC-blocked, summed into the loop.
    float exc = ins[0];
    if (!(exc > -1e6f && exc < 1e6f)) exc = 0.0f;
    exc = exciter_.process_lp(exc * 0.70710678f);
    exc += plucker_.process();
    exc = exc_dc_.process(exc);

    s += exc;
    s = fir_.process(s);
    s = iir_.process_lp(s);
    // Heal the loop: a NaN/inf write would otherwise circulate forever.
    if (!(s > -1e6f && s < 1e6f)) s = 0.0f;
    if (std::abs(s) < 1e-20f) s = 0.0f;  // denormal flush (render has no FTZ)
    line_.write(s);

    const float tap = line_.read_linear(comb_delay);
    outs[0] = s - tap;  // pluck-position comb (Rings' poly voice output)
    outs[1] = tap;
  }

  void copy_state_from(const Module& other) override {
    const auto& o = static_cast<const StringModule&>(other);
    line_.copy_from(o.line_);
    stretch_.copy_from(o.stretch_);
    fir_ = o.fir_;
    iir_ = o.iir_;
    exciter_ = o.exciter_;
    plucker_.copy_from(o.plucker_);
    noise_rng_ = o.noise_rng_;
    exc_dc_ = o.exc_dc_;
    bridge_dc_ = o.bridge_dc_;
    delay_ = o.delay_;
    delay_step_ = o.delay_step_;
    delay_target_ = o.delay_target_;
    position_ = o.position_;
    position_step_ = o.position_step_;
    dispersion_ = o.dispersion_;
    dispersion_step_ = o.dispersion_step_;
    comp_ = o.comp_;
    comp_step_ = o.comp_step_;
    f_ = o.f_;
    exc_fc_ = o.exc_fc_;
    noise_filter_ = o.noise_filter_;
    pos_raw_ = o.pos_raw_;
    dispersion_noise_ = o.dispersion_noise_;
    curved_bridge_ = o.curved_bridge_;
    prev_strike_ = o.prev_strike_;
    countdown_ = 0;
  }

 private:
  void update_block(const float* ins) {
    countdown_ = kBlock;
    const float freq = sane(ins[1], 0.0f, 24000.0f);
    const float voct = sane(ins[2], -10.0f, 10.0f);
    float f = freq * std::exp2(voct) / rate_;
    // Clamp through the delay line's reach: [4, kMaxDelay] samples.
    f = sane(f, 1.0f / kMaxDelay, 0.25f);
    f_ = f;
    delay_target_ = 1.0f / f;

    const float structure = sane(ins[4], 0.0f, 0.9995f);
    const float b_knob = sane(ins[5], 0.0f, 1.0f);
    const float damping = sane(ins[6], 0.0f, 1.0f);
    pos_raw_ = sane(ins[7], 0.0f, 1.0f);
    const float folded = 0.5f - 0.98f * std::abs(pos_raw_ - 0.5f);

    // Loop gain from target RT60 (string.cc): decay time exponential in
    // the damping knob, 0.07 s .. ~18 s.
    const float lf = damping * (2.0f - damping);
    const float rt60 = 0.07f * resonate::semitones_to_ratio(lf * 96.0f) * rate_;
    float g_loop = resonate::semitones_to_ratio(
        std::max(-120.0f * delay_target_ / rt60, -127.0f));
    float b = b_knob * b_knob;
    noise_filter_ = resonate::semitones_to_ratio((b_knob - 1.0f) * 48.0f);
    float damping_cutoff = std::min(
        24.0f + damping * damping * 48.0f + b_knob * b_knob * 24.0f, 84.0f);
    float damping_f =
        std::min(f * resonate::semitones_to_ratio(damping_cutoff), 0.499f);

    // Crossfade to a lossless loop at the top of the damping range.
    if (damping >= 0.95f) {
      const float to_inf = 20.0f * (damping - 0.95f);
      g_loop += to_inf * (1.0f - g_loop);
      b += to_inf * (1.0f - b);
      damping_f += to_inf * (0.4999f - damping_f);
      damping_cutoff += to_inf * (128.0f - damping_cutoff);
    }

    fir_.configure(g_loop, b, kBlock);
    iir_.set_f_q_accurate(damping_f, 0.5f);
    iir_.flush_denormals();
    exciter_.flush_denormals();
    if (std::abs(dispersion_noise_) < 1e-20f) dispersion_noise_ = 0.0f;
    if (std::abs(curved_bridge_) < 1e-20f) curved_bridge_ = 0.0f;

    // IIR phase-delay compensation: delay *= 1 - 2*atan(2^(-n/12))/(2*pi).
    const float shift =
        2.0f * std::atan(resonate::semitones_to_ratio(-damping_cutoff)) /
        (2.0f * std::numbers::pi_v<float>);
    const float comp_target = 1.0f - shift;

    const float disp_target = resonate::dispersion_from_structure(structure);

    // Exciter lowpass: same f-tracking brightness mapping as modal.
    const float c = b_knob * (2.0f - b_knob);
    exc_fc_ = std::clamp(f * resonate::semitones_to_ratio((c - 0.5f) * 96.0f),
                         1e-5f, 0.499f);
    exciter_.set_f_q_fast(exc_fc_, 1.5f);

    if (delay_ < 0.0f) {  // first block: snap, no glide from the sentinel
      delay_ = delay_target_;
      position_ = folded;
      dispersion_ = disp_target;
      comp_ = comp_target;
    }
    constexpr float inv = 1.0f / kBlock;
    delay_step_ = (delay_target_ - delay_) * inv;
    position_step_ = (folded - position_) * inv;
    dispersion_step_ = (disp_target - dispersion_) * inv;
    comp_step_ = (comp_target - comp_) * inv;
  }

  float rate_ = 48000.0f;
  resonate::DelayLine line_;
  resonate::DelayLine stretch_;
  resonate::DampingFir fir_;
  resonate::Svf iir_;
  resonate::Svf exciter_;
  resonate::Plucker plucker_;
  resonate::Rng noise_rng_{1u};
  resonate::DcBlocker exc_dc_;
  resonate::DcBlocker bridge_dc_;
  float delay_ = -1.0f;
  float delay_step_ = 0.0f;
  float delay_target_ = 218.0f;
  float position_ = 0.25f;
  float position_step_ = 0.0f;
  float dispersion_ = 0.0f;
  float dispersion_step_ = 0.0f;
  float comp_ = 1.0f;
  float comp_step_ = 0.0f;
  float f_ = 0.0f;
  float exc_fc_ = 0.05f;
  float noise_filter_ = 0.0f;
  float pos_raw_ = 0.25f;
  float dispersion_noise_ = 0.0f;
  float curved_bridge_ = 0.0f;
  float prev_strike_ = 0.0f;
  int countdown_ = 0;
};

std::unique_ptr<Module> make() { return std::make_unique<StringModule>(); }
}  // namespace

const ModuleInfo string_info{"string", kIns, kCfgs, kOuts, &make};
}  // namespace ms::dsp
```

Modify `src/dsp/modules.h` — after the `modal_info` extern, add:

```cpp
extern const ModuleInfo string_info;
```

Modify `src/dsp/registry.cpp` — append `&string_info,` after `&modal_info` in `kRegistry`.

- [ ] **Step 4: Add the docs entry**

In `docs/modules.md`, after the `## modal` section, and add `string` to the Quick palette "Sources" row:

```markdown
## `string`

`string { seed: 1 }` — extended Karplus-Strong string (Rings' string models).
Inputs `in` 0 (excitation audio, brightness-tracked lowpass + DC blocker),
`freq` 220 Hz, `v_oct`, rising-edge `strike` (seeded noise-burst pluck shaped
by position and brightness), `structure` 0.25 (bipolar dispersion: below 0.24
a curved-bridge nonlinearity — gongs, sitars; 0.24..0.26 pure string; above
0.26 stiff piano-like stretched partials; above ~0.9 adds rattle noise),
`brightness` 0.5 (loop FIR tone and exciter color), `damping` 0.5 (decay
0.07 s..~18 s, exponential; ≥ 0.95 crossfades to infinite sustain — a drone),
`position` 0.25 (pluck point and pickup comb). Outputs `out` (pickup-combed
voice, Rings' mix) and `aux` (raw pickup tap; `out + aux` is the bare loop).
Config `seed`: equal seeds repeat plucks exactly. Reaches ~5.9 Hz at 48 kHz.
```

- [ ] **Step 5: Run tests to verify they pass**

Run: `mise run test`
Expected: all tests pass, including `test_string` and docs coverage.

- [ ] **Step 6: Commit**

```bash
git add src/dsp/modules/string.cpp src/dsp/modules.h src/dsp/registry.cpp \
        docs/modules.md tests/test_string.cpp
git commit -m "feat: string resonator module (Rings Karplus-Strong models)"
```

---

### Task 4: Recipe, demo patch, spec scope update, final gates

**Files:**
- Modify: `docs/modules.md` (Recipes section)
- Create: `patches/rings.patch`
- Modify: `docs/superpowers/specs/2026-07-10-modsynth-design.md:223-227` (out-of-scope list)

**Interfaces:**
- Consumes: `modal` and `string` modules (Tasks 2–3) via the DSL.
- Produces: nothing consumed later; this is the closing task.

- [ ] **Step 1: Add the sympathetic-strings recipe**

In `docs/modules.md` under `## Recipes`, add:

```markdown
### Sympathetic strings (Rings recipe)

Rings' sympathetic model is just coupled strings — string 0 feeds the others
at low gain, tuned to intervals, ringing longer:

```text
main = string { seed: 2 }
s1 = string { seed: 3, damping: 0.85 }
s2 = string { seed: 4, damping: 0.85 }
seq -> main.v_oct
seq -> s1.v_oct
0.58496 -> s1.v_oct          # +7.02 st: a just fifth, log2(3/2)
seq -> s2.v_oct
1 -> s2.v_oct                # +1 octave
gate -> main.strike
main.out -> s1.in : 0.1      # Rings couples at 0.2/n
main.out -> s2.in : 0.1
main.out -> m.in1
s1.out -> m.in2 : 0.6
s2.out -> m.in3 : 0.6
```

Detune shimmer: duplicate a string and add `0.001 -> dup.v_oct`.
```

- [ ] **Step 2: Create the demo patch**

Create `patches/rings.patch`:

```text
# Rings-style plucked string + modal bell, generative.
clk = clock { bpm: 110 }
gate = euclid { steps: 8, hits: 3 }
sub = div { by: 4 }
mel = turing { len: 8, prob: 0.05, seed: 3 }
pitch = quant { scale: [0, 3, 5, 7, 10] }

pluck = string { seed: 7, damping: 0.7, position: 0.3 }
bell = modal { modes: 48, structure: 0.6, damping: 0.65, brightness: 0.4 }
verb = reverb { decay: 0.55, mix: 0.3 }

clk -> gate.clock
clk -> mel.clock
clk -> sub.in
mel.out -> pitch.in : 0.4
pitch -> pluck.v_oct
gate.gate -> pluck.strike
pitch -> bell.v_oct
-1 -> bell.v_oct
sub -> bell.strike

pluck.out -> verb.in_l : 0.5
pluck.out -> verb.in_r : 0.4
bell.out -> verb.in_r : 0.35
bell.out -> verb.in_l : 0.25
verb.out_l -> out.left
verb.out_r -> out.right
```

- [ ] **Step 3: Update the main design spec's scope list**

In `docs/superpowers/specs/2026-07-10-modsynth-design.md`, the out-of-scope
paragraph currently reads:

```text
Physical-modeling voice (Rings-alike), macro-oscillator
(Plaits-alike), dedicated drum voice, `push` manual-gate (needs a GUI panel),
wavetable VCO, module plugin ABI.
```

Replace with:

```text
Macro-oscillator (Plaits-alike), dedicated drum voice, `push` manual-gate
(needs a GUI panel), wavetable VCO, module plugin ABI. (The physical-modeling
voice landed as `modal`/`string`; see
`docs/superpowers/specs/2026-07-14-resonator-design.md`.)
```

- [ ] **Step 4: Run the full gates**

```bash
mise run test        # includes the auto-generated cli_check_rings patch test
mise run test-san    # ASan+UBSan
mise run ci          # warnings-as-errors
mise run render -- patches/rings.patch -d 10s -o /tmp/rings-demo.wav --stats
```

Expected: all pass; render stats show non-silent, non-clipping output
(peak below 1.0, RMS well above silence). Listen if in doubt.

- [ ] **Step 5: Commit**

```bash
git add docs/modules.md patches/rings.patch \
        docs/superpowers/specs/2026-07-10-modsynth-design.md
git commit -m "docs: sympathetic-strings recipe, rings demo patch, scope update"
```

---

## Self-Review Notes

- Spec §3/§4 constants were verified line-by-line against `rings/dsp/resonator.cc`, `string.cc`, `plucker.h`, `part.cc` (clones under the session scratchpad).
- Deliberate deviations from Rings, all recorded in the spec: 8192-sample line replaces the sub-11.7 Hz upsampler; strum/onset/normalization/limiter/reverb/polyphony omitted; the string always uses the f-tracking exciter mapping; the dispersion path always runs (dead zone reduces it to pure KS, keeping the RNG stream position-independent of structure).
- Type consistency: `resonate::` names in Tasks 2–3 match Task 1's declarations (`process_bp`, `process_lp`, `set_f_q_fast`, `set_f_q_accurate`, `read_linear`, `read_hermite`, `allpass`, `copy_from`, `configure`, `bipolar`, `trigger`).
- Both modules index inputs identically (0..7) — the tests' rig comments pin the order.
