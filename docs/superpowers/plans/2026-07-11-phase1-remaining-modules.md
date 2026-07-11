# Phase 1 Remaining Modules Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Implement the 12 remaining Phase 1 modules from the design spec — noise, math, xfade, drive, fold, sh, ladder, lpg, random, quant, mix, scope — completing the "drone/generative box" module palette.

**Architecture:** Each module is one small file in `src/dsp/modules/<name>.cpp` following the established pattern: anonymous-namespace `Module` subclass, `constexpr` port tables, `make()` factory, `const ModuleInfo <name>_info` at the bottom, plus an extern in `src/dsp/modules.h` and a pointer in `src/dsp/registry.cpp`'s `kRegistry`. Numeric params are patchable inputs (InputInfo with a default); enum/string/array params are ConfigInfo entries read in `init()`. One prerequisite task adds `cfg_num`/`cfg_arr` helpers beside the existing `cfg_str`.

**Tech Stack:** C++20, doctest, CMake (GLOB_RECURSE picks up new files automatically — no build edits needed). Build/test via `mise run build` / `mise run test`.

## Global Constraints

Copied from CLAUDE.md ("Non-negotiable design rules") and spec §4 — every task must obey:

- `Module::process()` never allocates, locks, or does I/O. `copy_state_from()` is RT-called: same rule.
- Everything is one signal: floats at audio rate. No control rate, no event type.
- No normalling. Modulation sums onto declared param bases; unpatched inputs read their default.
- Signal conventions: audio/bipolar ±1, unipolar CV 0..1, pitch `v_oct` in octaves, gates/triggers rise through 0.5.
- Modules are small and single-purpose.
- External deps stay at PipeWire + doctest. No new dependencies.
- Docs are part of the diff: spec table edits land in the same commit as the behavior.
- Conventional commit prefixes (feat/fix/test/chore/docs); tool-agnostic messages, no AI attribution.
- State-migration tests must be **load-bearing**: deleting any line of `copy_state_from` must fail a test. Post-copy input sequences must be chosen so a forgotten member copy visibly diverges (see the comments in `tests/test_migrate.cpp` for the trap to avoid: holding an edge input at 0 across the copy makes `prev_*` copies invisible).

**Reference implementations** (read before starting any task): `src/dsp/modules/vca.cpp` (config enum idiom), `src/dsp/modules/lfo.cpp` (phase + edge detection), `src/dsp/modules/svf.cpp` (filter + clamping), `src/dsp/module.h` (the API). Engine calls `process()` once per sample with `ins`/`outs` float arrays sized from the ModuleInfo port tables. `init()` and module construction run on the control thread (allocation OK there); `copy_state_from()` runs on the audio thread.

---

### Task 1: cfg_num and cfg_arr helpers

**Files:**
- Modify: `src/dsp/module.h` (after the `cfg_str` declaration, line 23)
- Modify: `src/dsp/module.cpp` (after the `cfg_str` definition)
- Test: `tests/test_module.cpp` (append)

**Interfaces:**
- Consumes: existing `Config` = `std::vector<std::pair<std::string, ConfigValue>>`; `ConfigValue{str, num, arr, is_num}`.
- Produces: `double cfg_num(const Config&, std::string_view name, double def)` — returns `v.num` when the entry exists and `is_num`, else `def`. `const std::vector<double>& cfg_arr(const Config&, std::string_view name)` — returns the entry's `arr` (empty vector if absent or not an array); the reference is only valid while the Config lives, so modules must copy what they keep.

- [ ] **Step 1: Write the failing tests** — append to `tests/test_module.cpp`:

```cpp
TEST_CASE("cfg_num returns numeric config values and defaults") {
  Config c;
  ConfigValue seven; seven.num = 7.0; seven.is_num = true;
  c.push_back({"seed", seven});
  ConfigValue name; name.str = "pink";
  c.push_back({"color", name});
  CHECK(cfg_num(c, "seed", 1.0) == doctest::Approx(7.0));
  CHECK(cfg_num(c, "missing", 42.0) == doctest::Approx(42.0));
  CHECK(cfg_num(c, "color", 3.0) == doctest::Approx(3.0));  // non-numeric -> default
}

TEST_CASE("cfg_arr returns array config values or an empty vector") {
  Config c;
  ConfigValue scale; scale.arr = {0.0, 3.0, 7.0};
  c.push_back({"scale", scale});
  CHECK(cfg_arr(c, "scale").size() == 3);
  CHECK(cfg_arr(c, "scale")[1] == doctest::Approx(3.0));
  CHECK(cfg_arr(c, "missing").empty());
}
```

- [ ] **Step 2: Run tests to verify they fail** — `mise run build` — Expected: compile error, `cfg_num` not declared.
- [ ] **Step 3: Implement** — in `src/dsp/module.h` after the `cfg_str` line:

```cpp
double cfg_num(const Config& c, std::string_view name, double def);
const std::vector<double>& cfg_arr(const Config& c, std::string_view name);
```

In `src/dsp/module.cpp` after `cfg_str`:

```cpp
double cfg_num(const Config& c, std::string_view name, double def) {
  for (const auto& [k, v] : c)
    if (k == name && v.is_num) return v.num;
  return def;
}

const std::vector<double>& cfg_arr(const Config& c, std::string_view name) {
  static const std::vector<double> kEmpty;
  for (const auto& [k, v] : c)
    if (k == name) return v.arr;
  return kEmpty;
}
```

- [ ] **Step 4: Run tests** — `mise run build && ./build/mstests --test-case="*cfg*"` — Expected: PASS.
- [ ] **Step 5: Commit** — `git add src/dsp/module.h src/dsp/module.cpp tests/test_module.cpp && git commit -m "feat: add cfg_num and cfg_arr config helpers"`

---

### Task 2: noise module

**Files:**
- Create: `src/dsp/modules/noise.cpp`
- Modify: `src/dsp/modules.h` (add `extern const ModuleInfo noise_info;`)
- Modify: `src/dsp/registry.cpp` (add `&noise_info` to `kRegistry`)
- Modify: `docs/superpowers/specs/2026-07-10-modsynth-design.md:139` — change the noise row's Key params from `` `color` (white\|pink) `` to `` `color` (white\|pink), `seed` `` (seed is needed so two noise instances decorrelate; docs are part of the diff).
- Test: `tests/test_noise.cpp`, append one case to `tests/test_migrate.cpp`

**Interfaces:**
- Consumes: `cfg_str`, `cfg_num` (Task 1).
- Produces: `noise_info` — type `"noise"`, inputs: none, configs: `color` (white|pink), `seed` (number, default 1), outputs: `out`. Bipolar ±1-ish (pink may slightly exceed; clamped to ±1.5).

- [ ] **Step 1: Write the failing tests** — `tests/test_noise.cpp`:

```cpp
#include <doctest/doctest.h>
#include <cmath>
#include "dsp/modules.h"

using namespace ms::dsp;

namespace {
std::unique_ptr<Module> make_noise(const char* color, double seed = 1.0) {
  const ModuleInfo* info = find_module("noise");
  REQUIRE(info != nullptr);
  auto m = info->create();
  Config cfg;
  ConfigValue c; c.str = color;
  cfg.push_back({"color", c});
  ConfigValue s; s.num = seed; s.is_num = true;
  cfg.push_back({"seed", s});
  m->init(48000.0, cfg);
  return m;
}
}  // namespace

TEST_CASE("noise is bounded, nonzero, and deterministic per seed") {
  auto a = make_noise("white", 7.0);
  auto b = make_noise("white", 7.0);
  auto c = make_noise("white", 8.0);
  float oa[1], ob[1], oc[1];
  bool differs_from_other_seed = false;
  double sumsq = 0.0;
  for (int i = 0; i < 4096; ++i) {
    a->process(nullptr, oa);
    b->process(nullptr, ob);
    c->process(nullptr, oc);
    CHECK(std::abs(oa[0]) <= 1.0f);
    CHECK(oa[0] == ob[0]);  // same seed, same stream
    if (oa[0] != oc[0]) differs_from_other_seed = true;
    sumsq += static_cast<double>(oa[0]) * oa[0];
  }
  CHECK(differs_from_other_seed);
  CHECK(std::sqrt(sumsq / 4096.0) > 0.1);  // actually noisy, not near-silent
}

TEST_CASE("pink noise is smoother than white noise") {
  auto white = make_noise("white");
  auto pink = make_noise("pink");
  float ow[1], op[1];
  float prev_w = 0.0f, prev_p = 0.0f;
  double dw = 0.0, dp = 0.0;
  for (int i = 0; i < 8192; ++i) {
    white->process(nullptr, ow);
    pink->process(nullptr, op);
    if (i > 0) {
      dw += std::abs(ow[0] - prev_w);
      dp += std::abs(op[0] - prev_p);
    }
    prev_w = ow[0];
    prev_p = op[0];
    CHECK(std::abs(op[0]) <= 1.5f);
  }
  CHECK(dp < dw * 0.5);  // pink: less high-frequency energy
}
```

- [ ] **Step 2: Run to verify failure** — `mise run build && ./build/mstests --test-case="*noise*"` — Expected: FAIL, `find_module("noise")` returns null (REQUIRE fires).
- [ ] **Step 3: Implement** — `src/dsp/modules/noise.cpp`:

```cpp
#include <algorithm>
#include <cstdint>
#include "dsp/modules.h"

namespace ms::dsp {
namespace {

constexpr const char* kColors[] = {"white", "pink", nullptr};
constexpr ConfigInfo kCfg[] = {{"color", kColors}, {"seed", nullptr}};
constexpr OutputInfo kOuts[] = {{"out"}};

class Noise final : public Module {
 public:
  void init(double, const Config& cfg) override {
    pink_ = cfg_str(cfg, "color", "white") == "pink";
    const auto seed = static_cast<std::uint32_t>(cfg_num(cfg, "seed", 1.0));
    state_ = seed * 2654435761u;
    if (state_ == 0) state_ = 0x9E3779B9u;
    b0_ = b1_ = b2_ = 0.0f;
  }

  void process(const float*, float* outs) override {
    std::uint32_t x = state_;
    x ^= x << 13;
    x ^= x >> 17;
    x ^= x << 5;
    state_ = x;
    const float w = 2.0f * (static_cast<float>(x) * (1.0f / 4294967296.0f)) - 1.0f;
    if (!pink_) {
      outs[0] = w;
      return;
    }
    // Paul Kellet's economy pink filter (-3 dB/oct approximation).
    b0_ = 0.99765f * b0_ + w * 0.0990460f;
    b1_ = 0.96300f * b1_ + w * 0.2965164f;
    b2_ = 0.57000f * b2_ + w * 1.0526913f;
    outs[0] = std::clamp((b0_ + b1_ + b2_ + w * 0.1848f) * 0.11f, -1.5f, 1.5f);
  }

  void copy_state_from(const Module& other) override {
    const auto& o = static_cast<const Noise&>(other);
    state_ = o.state_;
    b0_ = o.b0_;
    b1_ = o.b1_;
    b2_ = o.b2_;
  }

 private:
  bool pink_ = false;
  std::uint32_t state_ = 0x9E3779B9u;
  float b0_ = 0.0f, b1_ = 0.0f, b2_ = 0.0f;
};

std::unique_ptr<Module> make() { return std::make_unique<Noise>(); }
}  // namespace

const ModuleInfo noise_info{"noise", {}, kCfg, kOuts, &make};
}  // namespace ms::dsp
```

Add to `src/dsp/modules.h`: `extern const ModuleInfo noise_info;`. Add `&noise_info` to `kRegistry` in `src/dsp/registry.cpp`. Update the spec row as listed under Files.

- [ ] **Step 4: Add the migration test** — append to `tests/test_migrate.cpp` (RNG stream continuity is inherently load-bearing — a fresh instance's stream diverges immediately):

```cpp
TEST_CASE("noise migrates rng and pink filter state") {
  const ModuleInfo* info = find_module("noise");
  REQUIRE(info != nullptr);
  auto a = info->create();
  auto b = info->create();
  Config cfg;
  ConfigValue c; c.str = "pink";
  cfg.push_back({"color", c});
  a->init(48000.0, cfg);
  b->init(48000.0, cfg);
  float out_a[1], out_b[1];
  for (int i = 0; i < 500; ++i) a->process(nullptr, out_a);
  b->copy_state_from(*a);
  for (int i = 0; i < 8; ++i) {
    a->process(nullptr, out_a);
    b->process(nullptr, out_b);
    CHECK(out_a[0] == doctest::Approx(out_b[0]));
  }
}
```

- [ ] **Step 5: Run tests** — `mise run build && ./build/mstests` — Expected: all PASS.
- [ ] **Step 6: Commit** — `git add src/dsp/modules/noise.cpp src/dsp/modules.h src/dsp/registry.cpp tests/test_noise.cpp tests/test_migrate.cpp docs/superpowers/specs/2026-07-10-modsynth-design.md && git commit -m "feat: add noise module (white/pink, seedable)"`

---

### Task 3: math module

**Files:**
- Create: `src/dsp/modules/math.cpp`
- Modify: `src/dsp/modules.h`, `src/dsp/registry.cpp` (same pattern as Task 2: extern + registry entry)
- Test: `tests/test_math.cpp`

**Interfaces:**
- Produces: `math_info` — type `"math"`, inputs `a` (0), `b` (1), config `op` (add|sub|mult|min|max|abs|clip|wrap), output `out`. `b` defaults to 1 so `mult`/`clip`/`wrap` are passthrough-neutral when unpatched. `clip` clamps `a` to ±|b|; `wrap` wraps `a` into [-|b|, |b|) (outputs 0 when b == 0); `abs` ignores `b`. Stateless — no `copy_state_from`.

- [ ] **Step 1: Write the failing tests** — `tests/test_math.cpp`:

```cpp
#include <doctest/doctest.h>
#include "dsp/modules.h"

using namespace ms::dsp;

namespace {
float run_op(const char* op, float a, float b) {
  const ModuleInfo* info = find_module("math");
  REQUIRE(info != nullptr);
  auto m = info->create();
  Config cfg;
  ConfigValue v; v.str = op;
  cfg.push_back({"op", v});
  m->init(48000.0, cfg);
  const float ins[2] = {a, b};
  float out[1];
  m->process(ins, out);
  return out[0];
}
}  // namespace

TEST_CASE("math ops compute per-sample results") {
  CHECK(run_op("add", 0.25f, 0.5f) == doctest::Approx(0.75f));
  CHECK(run_op("sub", 0.25f, 0.5f) == doctest::Approx(-0.25f));
  CHECK(run_op("mult", 0.5f, -0.5f) == doctest::Approx(-0.25f));  // ring mod
  CHECK(run_op("min", 0.3f, -0.2f) == doctest::Approx(-0.2f));
  CHECK(run_op("max", 0.3f, -0.2f) == doctest::Approx(0.3f));
  CHECK(run_op("abs", -0.7f, 0.0f) == doctest::Approx(0.7f));  // full-wave rectifier
}

TEST_CASE("math clip clamps to plus-minus b") {
  CHECK(run_op("clip", 0.9f, 0.5f) == doctest::Approx(0.5f));
  CHECK(run_op("clip", -0.9f, 0.5f) == doctest::Approx(-0.5f));
  CHECK(run_op("clip", 0.3f, 0.5f) == doctest::Approx(0.3f));
}

TEST_CASE("math wrap folds into the plus-minus b range") {
  CHECK(run_op("wrap", 0.6f, 0.5f) == doctest::Approx(-0.4f));
  CHECK(run_op("wrap", -0.6f, 0.5f) == doctest::Approx(0.4f));
  CHECK(run_op("wrap", 0.2f, 0.5f) == doctest::Approx(0.2f));
  CHECK(run_op("wrap", 0.2f, 0.0f) == doctest::Approx(0.0f));  // degenerate range
}

TEST_CASE("math b defaults to one") {
  const ModuleInfo* info = find_module("math");
  REQUIRE(info->input_index("b") == 1);
  CHECK(info->inputs[1].def == doctest::Approx(1.0f));
}
```

- [ ] **Step 2: Verify failure** — `mise run build && ./build/mstests --test-case="*math*"` — Expected: FAIL (unknown module).
- [ ] **Step 3: Implement** — `src/dsp/modules/math.cpp`:

```cpp
#include <algorithm>
#include <cmath>
#include "dsp/modules.h"

namespace ms::dsp {
namespace {

constexpr InputInfo kIns[] = {{"a", 0.f}, {"b", 1.f}};
constexpr const char* kOps[] = {"add", "sub", "mult", "min", "max",
                                "abs", "clip", "wrap", nullptr};
constexpr ConfigInfo kCfg[] = {{"op", kOps}};
constexpr OutputInfo kOuts[] = {{"out"}};

class MathModule final : public Module {
 public:
  void init(double, const Config& cfg) override {
    const std::string op = cfg_str(cfg, "op", "add");
    for (int i = 0; kOps[i]; ++i)
      if (op == kOps[i]) op_ = i;
  }

  void process(const float* ins, float* outs) override {
    const float a = ins[0];
    const float b = ins[1];
    switch (op_) {
      case 0: outs[0] = a + b; break;
      case 1: outs[0] = a - b; break;
      case 2: outs[0] = a * b; break;
      case 3: outs[0] = std::min(a, b); break;
      case 4: outs[0] = std::max(a, b); break;
      case 5: outs[0] = std::abs(a); break;
      case 6: {
        const float r = std::abs(b);
        outs[0] = std::clamp(a, -r, r);
        break;
      }
      case 7: {
        const float r = std::abs(b);
        if (r <= 0.0f) { outs[0] = 0.0f; break; }
        float x = std::fmod(a + r, 2.0f * r);
        if (x < 0.0f) x += 2.0f * r;
        outs[0] = x - r;
        break;
      }
    }
  }

 private:
  int op_ = 0;
};

std::unique_ptr<Module> make() { return std::make_unique<MathModule>(); }
}  // namespace

const ModuleInfo math_info{"math", kIns, kCfg, kOuts, &make};
}  // namespace ms::dsp
```

Add extern to `modules.h`, `&math_info` to `kRegistry`.

- [ ] **Step 4: Run tests** — Expected: PASS.
- [ ] **Step 5: Commit** — `git commit -m "feat: add math module (add/sub/mult/min/max/abs/clip/wrap)"` (add the four touched files).

---

### Task 4: xfade module

**Files:**
- Create: `src/dsp/modules/xfade.cpp`
- Modify: `src/dsp/modules.h`, `src/dsp/registry.cpp`
- Test: `tests/test_xfade.cpp`

**Interfaces:**
- Produces: `xfade_info` — type `"xfade"`, inputs `a` (0), `b` (0), `mix` (0.5), output `out`. Equal-power: `out = a*cos(m*pi/2) + b*sin(m*pi/2)`, `m = clamp(mix, 0, 1)`. Stateless.

- [ ] **Step 1: Write the failing tests** — `tests/test_xfade.cpp`:

```cpp
#include <doctest/doctest.h>
#include <cmath>
#include "dsp/modules.h"

using namespace ms::dsp;

namespace {
float run_mix(float a, float b, float mix) {
  const ModuleInfo* info = find_module("xfade");
  REQUIRE(info != nullptr);
  auto m = info->create();
  m->init(48000.0, {});
  const float ins[3] = {a, b, mix};
  float out[1];
  m->process(ins, out);
  return out[0];
}
}  // namespace

TEST_CASE("xfade endpoints pass one input exclusively") {
  CHECK(run_mix(0.8f, -0.3f, 0.0f) == doctest::Approx(0.8f));
  CHECK(run_mix(0.8f, -0.3f, 1.0f) == doctest::Approx(-0.3f));
  CHECK(run_mix(0.8f, -0.3f, -2.0f) == doctest::Approx(0.8f));  // clamped
  CHECK(run_mix(0.8f, -0.3f, 3.0f) == doctest::Approx(-0.3f));  // clamped
}

TEST_CASE("xfade midpoint is equal-power") {
  const float g = run_mix(1.0f, 0.0f, 0.5f);
  const float h = run_mix(0.0f, 1.0f, 0.5f);
  CHECK(g == doctest::Approx(std::sqrt(0.5f)).epsilon(0.001));
  CHECK(h == doctest::Approx(std::sqrt(0.5f)).epsilon(0.001));
  CHECK(g * g + h * h == doctest::Approx(1.0f).epsilon(0.001));
}
```

- [ ] **Step 2: Verify failure** — Expected: FAIL (unknown module).
- [ ] **Step 3: Implement** — `src/dsp/modules/xfade.cpp`:

```cpp
#include <algorithm>
#include <cmath>
#include <numbers>
#include "dsp/modules.h"

namespace ms::dsp {
namespace {

constexpr InputInfo kIns[] = {{"a", 0.f}, {"b", 0.f}, {"mix", 0.5f}};
constexpr OutputInfo kOuts[] = {{"out"}};

class Xfade final : public Module {
 public:
  void process(const float* ins, float* outs) override {
    const float m = std::clamp(ins[2], 0.0f, 1.0f);
    const float angle = m * (std::numbers::pi_v<float> / 2.0f);
    outs[0] = ins[0] * std::cos(angle) + ins[1] * std::sin(angle);
  }
};

std::unique_ptr<Module> make() { return std::make_unique<Xfade>(); }
}  // namespace

const ModuleInfo xfade_info{"xfade", kIns, {}, kOuts, &make};
}  // namespace ms::dsp
```

Extern + registry entry as usual.

- [ ] **Step 4: Run tests** — Expected: PASS.
- [ ] **Step 5: Commit** — `git commit -m "feat: add xfade equal-power crossfader"`

---

### Task 5: drive module

**Files:**
- Create: `src/dsp/modules/drive.cpp`
- Modify: `src/dsp/modules.h`, `src/dsp/registry.cpp`
- Test: `tests/test_drive.cpp`

**Interfaces:**
- Produces: `drive_info` — type `"drive"`, inputs `in` (0), `amount` (0.5), config `mode` (soft|hard), output `out`. Pre-gain `g = 1 + 9*clamp(amount, 0, 1)`. soft: `tanh(g*in)`; hard: `clamp(g*in, -1, 1)`. Stateless.

- [ ] **Step 1: Write the failing tests** — `tests/test_drive.cpp`:

```cpp
#include <doctest/doctest.h>
#include <cmath>
#include "dsp/modules.h"

using namespace ms::dsp;

namespace {
float run_drive(const char* mode, float in, float amount) {
  const ModuleInfo* info = find_module("drive");
  REQUIRE(info != nullptr);
  auto m = info->create();
  Config cfg;
  ConfigValue v; v.str = mode;
  cfg.push_back({"mode", v});
  m->init(48000.0, cfg);
  const float ins[2] = {in, amount};
  float out[1];
  m->process(ins, out);
  return out[0];
}
}  // namespace

TEST_CASE("soft drive saturates smoothly and stays bounded") {
  CHECK(run_drive("soft", 0.0f, 1.0f) == doctest::Approx(0.0f));
  CHECK(std::abs(run_drive("soft", 1.0f, 1.0f)) < 1.0f);   // tanh never reaches 1
  CHECK(run_drive("soft", 1.0f, 1.0f) > 0.99f);            // but gets close at g=10
  CHECK(run_drive("soft", -1.0f, 1.0f) == doctest::Approx(-run_drive("soft", 1.0f, 1.0f)));
  // More amount -> more output for the same small input (gain rises)
  CHECK(run_drive("soft", 0.05f, 1.0f) > run_drive("soft", 0.05f, 0.0f));
}

TEST_CASE("hard drive clips exactly at plus-minus one") {
  CHECK(run_drive("hard", 0.5f, 1.0f) == doctest::Approx(1.0f));    // 0.5*10 clipped
  CHECK(run_drive("hard", -0.5f, 1.0f) == doctest::Approx(-1.0f));
  CHECK(run_drive("hard", 0.05f, 1.0f) == doctest::Approx(0.5f));   // linear below knee
}

TEST_CASE("drive amount zero is near-unity gain") {
  CHECK(run_drive("hard", 0.3f, 0.0f) == doctest::Approx(0.3f));
  CHECK(run_drive("soft", 0.1f, 0.0f) == doctest::Approx(std::tanh(0.1f)));
}
```

- [ ] **Step 2: Verify failure** — Expected: FAIL (unknown module).
- [ ] **Step 3: Implement** — `src/dsp/modules/drive.cpp`:

```cpp
#include <algorithm>
#include <cmath>
#include "dsp/modules.h"

namespace ms::dsp {
namespace {

constexpr InputInfo kIns[] = {{"in", 0.f}, {"amount", 0.5f}};
constexpr const char* kModes[] = {"soft", "hard", nullptr};
constexpr ConfigInfo kCfg[] = {{"mode", kModes}};
constexpr OutputInfo kOuts[] = {{"out"}};

class Drive final : public Module {
 public:
  void init(double, const Config& cfg) override {
    hard_ = cfg_str(cfg, "mode", "soft") == "hard";
  }

  void process(const float* ins, float* outs) override {
    const float g = 1.0f + 9.0f * std::clamp(ins[1], 0.0f, 1.0f);
    const float x = ins[0] * g;
    outs[0] = hard_ ? std::clamp(x, -1.0f, 1.0f) : std::tanh(x);
  }

 private:
  bool hard_ = false;
};

std::unique_ptr<Module> make() { return std::make_unique<Drive>(); }
}  // namespace

const ModuleInfo drive_info{"drive", kIns, kCfg, kOuts, &make};
}  // namespace ms::dsp
```

Extern + registry entry.

- [ ] **Step 4: Run tests** — Expected: PASS.
- [ ] **Step 5: Commit** — `git commit -m "feat: add drive saturation module (soft/hard)"`

---

### Task 6: fold module

**Files:**
- Create: `src/dsp/modules/fold.cpp`
- Modify: `src/dsp/modules.h`, `src/dsp/registry.cpp`
- Test: `tests/test_fold.cpp`

**Interfaces:**
- Produces: `fold_info` — type `"fold"`, inputs `in` (0), `fold` (1), `sym` (0), output `out`. `x = in * max(fold, 0) + sym`, then triangle-fold into ±1: `out = 1 - 4*|frac((x+1)/4) - 0.5|`. Identity for |x| ≤ 1. Stateless.

- [ ] **Step 1: Write the failing tests** — `tests/test_fold.cpp`:

```cpp
#include <doctest/doctest.h>
#include <cmath>
#include "dsp/modules.h"

using namespace ms::dsp;

namespace {
float run_fold(float in, float fold, float sym = 0.0f) {
  const ModuleInfo* info = find_module("fold");
  REQUIRE(info != nullptr);
  auto m = info->create();
  m->init(48000.0, {});
  const float ins[3] = {in, fold, sym};
  float out[1];
  m->process(ins, out);
  return out[0];
}
}  // namespace

TEST_CASE("fold is identity inside plus-minus one at unity gain") {
  CHECK(run_fold(0.0f, 1.0f) == doctest::Approx(0.0f));
  CHECK(run_fold(0.5f, 1.0f) == doctest::Approx(0.5f));
  CHECK(run_fold(-0.5f, 1.0f) == doctest::Approx(-0.5f));
  CHECK(run_fold(1.0f, 1.0f) == doctest::Approx(1.0f));
  CHECK(run_fold(-1.0f, 1.0f) == doctest::Approx(-1.0f));
}

TEST_CASE("fold reflects overshoot back into range") {
  CHECK(run_fold(1.5f, 1.0f) == doctest::Approx(0.5f));   // 1.5 folds to 0.5
  CHECK(run_fold(-1.5f, 1.0f) == doctest::Approx(-0.5f));
  CHECK(run_fold(0.75f, 2.0f) == doctest::Approx(0.5f));  // gain 2: 1.5 -> 0.5
  CHECK(run_fold(3.0f, 1.0f) == doctest::Approx(-1.0f));  // double fold
  for (float x = -6.0f; x <= 6.0f; x += 0.13f)
    CHECK(std::abs(run_fold(x, 1.0f)) <= 1.0f + 1e-6f);
}

TEST_CASE("sym offsets the input before folding") {
  CHECK(run_fold(0.8f, 1.0f, 0.5f) == doctest::Approx(0.7f));  // 1.3 folds to 0.7
  CHECK(run_fold(0.0f, 1.0f, 0.25f) == doctest::Approx(0.25f));
}
```

- [ ] **Step 2: Verify failure** — Expected: FAIL (unknown module).
- [ ] **Step 3: Implement** — `src/dsp/modules/fold.cpp`:

```cpp
#include <algorithm>
#include <cmath>
#include "dsp/modules.h"

namespace ms::dsp {
namespace {

constexpr InputInfo kIns[] = {{"in", 0.f}, {"fold", 1.f}, {"sym", 0.f}};
constexpr OutputInfo kOuts[] = {{"out"}};

class Fold final : public Module {
 public:
  void process(const float* ins, float* outs) override {
    const float x = ins[0] * std::max(ins[1], 0.0f) + ins[2];
    float t = (x + 1.0f) * 0.25f;
    t -= std::floor(t);
    outs[0] = 1.0f - 4.0f * std::abs(t - 0.5f);
  }
};

std::unique_ptr<Module> make() { return std::make_unique<Fold>(); }
}  // namespace

const ModuleInfo fold_info{"fold", kIns, {}, kOuts, &make};
}  // namespace ms::dsp
```

Extern + registry entry.

- [ ] **Step 4: Run tests** — Expected: PASS.
- [ ] **Step 5: Commit** — `git commit -m "feat: add fold wavefolder module"`

---

### Task 7: sh module (sample & hold / track & hold)

**Files:**
- Create: `src/dsp/modules/sh.cpp`
- Modify: `src/dsp/modules.h`, `src/dsp/registry.cpp`
- Test: `tests/test_sh.cpp`, append two cases to `tests/test_migrate.cpp`

**Interfaces:**
- Produces: `sh_info` — type `"sh"`, inputs `in` (0), `trig` (0), config `mode` (sample|track), output `out`. sample: latch `in` on trig rising through 0.5. track: follow `in` while trig > 0.5, hold when low. State: `held_`, `prev_trig_`.

- [ ] **Step 1: Write the failing tests** — `tests/test_sh.cpp`:

```cpp
#include <doctest/doctest.h>
#include "dsp/modules.h"

using namespace ms::dsp;

namespace {
struct ShRig {
  std::unique_ptr<Module> module;
  float ins[2] = {0.0f, 0.0f};  // in, trig

  explicit ShRig(const char* mode) {
    const ModuleInfo* info = find_module("sh");
    REQUIRE(info != nullptr);
    module = info->create();
    Config cfg;
    ConfigValue v; v.str = mode;
    cfg.push_back({"mode", v});
    module->init(48000.0, cfg);
  }

  float tick(float in, float trig) {
    ins[0] = in;
    ins[1] = trig;
    float out[1];
    module->process(ins, out);
    return out[0];
  }
};
}  // namespace

TEST_CASE("sample mode latches only on the trigger's rising edge") {
  ShRig sh("sample");
  CHECK(sh.tick(0.3f, 0.0f) == doctest::Approx(0.0f));  // nothing latched yet
  CHECK(sh.tick(0.3f, 1.0f) == doctest::Approx(0.3f));  // edge: latch
  CHECK(sh.tick(0.9f, 1.0f) == doctest::Approx(0.3f));  // held high: no re-latch
  CHECK(sh.tick(0.9f, 0.0f) == doctest::Approx(0.3f));  // low: hold
  CHECK(sh.tick(-0.4f, 1.0f) == doctest::Approx(-0.4f));  // next edge: latch again
}

TEST_CASE("track mode follows while the gate is high and holds when low") {
  ShRig sh("track");
  CHECK(sh.tick(0.2f, 1.0f) == doctest::Approx(0.2f));
  CHECK(sh.tick(0.6f, 1.0f) == doctest::Approx(0.6f));  // following
  CHECK(sh.tick(0.9f, 0.0f) == doctest::Approx(0.6f));  // gate low: hold
  CHECK(sh.tick(-0.1f, 0.0f) == doctest::Approx(0.6f));
  CHECK(sh.tick(-0.1f, 1.0f) == doctest::Approx(-0.1f));  // reopens
}
```

- [ ] **Step 2: Verify failure** — Expected: FAIL (unknown module).
- [ ] **Step 3: Implement** — `src/dsp/modules/sh.cpp`:

```cpp
#include "dsp/modules.h"

namespace ms::dsp {
namespace {

constexpr InputInfo kIns[] = {{"in", 0.f}, {"trig", 0.f}};
constexpr const char* kModes[] = {"sample", "track", nullptr};
constexpr ConfigInfo kCfg[] = {{"mode", kModes}};
constexpr OutputInfo kOuts[] = {{"out"}};

class Sh final : public Module {
 public:
  void init(double, const Config& cfg) override {
    track_ = cfg_str(cfg, "mode", "sample") == "track";
    held_ = 0.0f;
    prev_trig_ = 0.0f;
  }

  void process(const float* ins, float* outs) override {
    if (track_) {
      if (ins[1] > 0.5f) held_ = ins[0];
    } else {
      if (ins[1] > 0.5f && prev_trig_ <= 0.5f) held_ = ins[0];
    }
    prev_trig_ = ins[1];
    outs[0] = held_;
  }

  void copy_state_from(const Module& other) override {
    const auto& o = static_cast<const Sh&>(other);
    held_ = o.held_;
    prev_trig_ = o.prev_trig_;
  }

 private:
  bool track_ = false;
  float held_ = 0.0f;
  float prev_trig_ = 0.0f;
};

std::unique_ptr<Module> make() { return std::make_unique<Sh>(); }
}  // namespace

const ModuleInfo sh_info{"sh", kIns, kCfg, kOuts, &make};
}  // namespace ms::dsp
```

Extern + registry entry.

- [ ] **Step 4: Add load-bearing migration tests** — append to `tests/test_migrate.cpp`. The held-value copy is exercised by comparing outputs while the trigger is low; the `prev_trig_` copy is exercised by copying with the trigger held high and a *different* value at the input (a forgotten copy re-latches):

```cpp
TEST_CASE("sh migrates held value and prev_trig_ edge memory") {
  const ModuleInfo* info = find_module("sh");
  REQUIRE(info != nullptr);
  auto a = info->create();
  auto b = info->create();
  a->init(48000.0, {});
  b->init(48000.0, {});
  float out_a[1], out_b[1];
  float ins[2] = {0.55f, 0.0f};
  a->process(ins, out_a);
  ins[1] = 1.0f;  // rising edge: a latches 0.55, prev_trig_ becomes 1.0
  a->process(ins, out_a);
  b->copy_state_from(*a);
  // Trigger stays high but the input changes: a fresh prev_trig_ (0.0)
  // would see a spurious edge and latch -0.9 instead of holding 0.55.
  ins[0] = -0.9f;
  for (int i = 0; i < 8; ++i) {
    a->process(ins, out_a);
    b->process(ins, out_b);
    CHECK(out_a[0] == doctest::Approx(out_b[0]));
    CHECK(out_a[0] == doctest::Approx(0.55f));
  }
}
```

- [ ] **Step 5: Run tests** — Expected: PASS. Sanity: temporarily delete `prev_trig_ = o.prev_trig_;`, expect the migrate test to fail, restore.
- [ ] **Step 6: Commit** — `git commit -m "feat: add sh sample/track-and-hold module"`

---

### Task 8: ladder module

**Files:**
- Create: `src/dsp/modules/ladder.cpp`
- Modify: `src/dsp/modules.h`, `src/dsp/registry.cpp`
- Test: `tests/test_ladder.cpp`, append one case to `tests/test_migrate.cpp`

**Interfaces:**
- Produces: `ladder_info` — type `"ladder"`, inputs `in` (0), `cutoff` (1000), `res` (0), `drive` (0), output `out`. Zavalishin TPT/ZDF 4-pole: per-stage `G = g/(1+g)`, `g = tan(pi*fc/fs)`; zero-delay resonance loop solved algebraically with `k = 4*res`; input stage `tanh(in * (1 + 4*drive))`. State: `s1_..s4_`.

- [ ] **Step 1: Write the failing tests** — `tests/test_ladder.cpp`:

```cpp
#include <doctest/doctest.h>
#include <cmath>
#include <numbers>
#include "dsp/modules.h"

using namespace ms::dsp;

namespace {
std::unique_ptr<Module> make_ladder() {
  const ModuleInfo* info = find_module("ladder");
  REQUIRE(info != nullptr);
  auto m = info->create();
  m->init(48000.0, {});
  return m;
}

double ladder_rms(float frequency, float cutoff, float res) {
  auto m = make_ladder();
  float ins[4] = {0.0f, cutoff, res, 0.0f};
  float out[1];
  double sum = 0.0;
  constexpr int warmup = 4096;
  constexpr int count = 48000;
  for (int i = 0; i < warmup + count; ++i) {
    ins[0] = 0.5f * std::sin(static_cast<float>(
        2.0 * std::numbers::pi * frequency * i / 48000.0));
    m->process(ins, out);
    if (i >= warmup) sum += static_cast<double>(out[0]) * out[0];
  }
  return std::sqrt(sum / count);
}
}  // namespace

TEST_CASE("ladder settles to DC with no resonance") {
  auto m = make_ladder();
  const float ins[4] = {0.5f, 1000.0f, 0.0f, 0.0f};
  float out[1];
  for (int i = 0; i < 48000; ++i) m->process(ins, out);
  CHECK(out[0] == doctest::Approx(0.5f).epsilon(0.01));
}

TEST_CASE("ladder rolls off high frequencies steeply") {
  const double low = ladder_rms(100.0f, 1000.0f, 0.0f);
  const double high = ladder_rms(10000.0f, 1000.0f, 0.0f);
  CHECK(low > 0.3);
  CHECK(high < low * 0.001);  // ~24 dB/oct: >60 dB down at 10x cutoff
}

TEST_CASE("ladder resonance boosts near the cutoff") {
  const double flat = ladder_rms(1000.0f, 1000.0f, 0.0f);
  const double peaked = ladder_rms(1000.0f, 1000.0f, 0.85f);
  CHECK(peaked > flat * 1.5);
}

TEST_CASE("ladder stays finite under extreme modulation") {
  auto m = make_ladder();
  float ins[4] = {0.0f, 0.0f, 0.0f, 0.0f};
  float out[1];
  for (int i = 0; i < 100000; ++i) {
    ins[0] = i % 2 == 0 ? 1.0f : -1.0f;
    ins[1] = i % 3 == 0 ? -500.0f : 900000.0f;
    ins[2] = i % 5 == 0 ? -4.0f : 4.0f;
    ins[3] = i % 7 == 0 ? 10.0f : 0.0f;
    m->process(ins, out);
    if (!std::isfinite(out[0]) || std::abs(out[0]) >= 100.0f) {
      CAPTURE(i);
      REQUIRE(false);
    }
  }
}
```

- [ ] **Step 2: Verify failure** — Expected: FAIL (unknown module).
- [ ] **Step 3: Implement** — `src/dsp/modules/ladder.cpp`:

```cpp
#include <algorithm>
#include <cmath>
#include <numbers>
#include "dsp/modules.h"

namespace ms::dsp {
namespace {

constexpr InputInfo kIns[] = {
    {"in", 0.f}, {"cutoff", 1000.f}, {"res", 0.f}, {"drive", 0.f}};
constexpr OutputInfo kOuts[] = {{"out"}};

class Ladder final : public Module {
 public:
  void init(double rate, const Config&) override {
    rate_ = static_cast<float>(rate);
    s1_ = s2_ = s3_ = s4_ = 0.0f;
  }

  void process(const float* ins, float* outs) override {
    const float cutoff = std::clamp(ins[1], 5.0f, 0.45f * rate_);
    const float res = std::clamp(ins[2], 0.0f, 1.0f);
    const float drive = std::clamp(ins[3], 0.0f, 4.0f);
    const float g = std::tan(std::numbers::pi_v<float> * cutoff / rate_);
    const float G = g / (1.0f + g);
    const float k = 4.0f * res;
    const float x = std::tanh(ins[0] * (1.0f + 4.0f * drive));

    // TPT one-pole per stage: y = G*x + (1-G)*s.  Zero-delay feedback:
    // y4 = G^4*u + S with u = x - k*y4, solved for y4 before updating states.
    const float G2 = G * G;
    const float zf = 1.0f - G;
    const float S = G2 * G * zf * s1_ + G2 * zf * s2_ + G * zf * s3_ + zf * s4_;
    const float y4 = (G2 * G2 * x + S) / (1.0f + k * G2 * G2);
    const float u = x - k * y4;

    const float y1 = G * u + zf * s1_;
    s1_ = 2.0f * y1 - s1_;
    const float y2 = G * y1 + zf * s2_;
    s2_ = 2.0f * y2 - s2_;
    const float y3 = G * y2 + zf * s3_;
    s3_ = 2.0f * y3 - s3_;
    const float y4b = G * y3 + zf * s4_;
    s4_ = 2.0f * y4b - s4_;

    outs[0] = y4b;
  }

  void copy_state_from(const Module& other) override {
    const auto& o = static_cast<const Ladder&>(other);
    s1_ = o.s1_;
    s2_ = o.s2_;
    s3_ = o.s3_;
    s4_ = o.s4_;
  }

 private:
  float rate_ = 48000.0f;
  float s1_ = 0.0f, s2_ = 0.0f, s3_ = 0.0f, s4_ = 0.0f;
};

std::unique_ptr<Module> make() { return std::make_unique<Ladder>(); }
}  // namespace

const ModuleInfo ladder_info{"ladder", kIns, {}, kOuts, &make};
}  // namespace ms::dsp
```

Extern + registry entry.

- [ ] **Step 4: Add load-bearing migration test** — append to `tests/test_migrate.cpp`. Use a square-wave input so all four integrator states are far from zero at the copy point (a settled DC input would let differences hide; see the svf test comment above it):

```cpp
TEST_CASE("ladder migrates all four stage states") {
  const ModuleInfo* info = find_module("ladder");
  REQUIRE(info != nullptr);
  auto a = info->create();
  auto b = info->create();
  a->init(48000.0, {});
  b->init(48000.0, {});
  float ins[4] = {0.0f, 800.0f, 0.6f, 0.0f};
  float out_a[1] = {0}, out_b[1] = {0};
  for (int i = 0; i < 1000; ++i) {
    ins[0] = (i / 24) % 2 == 0 ? 0.7f : -0.7f;
    a->process(ins, out_a);
  }
  b->copy_state_from(*a);
  for (int i = 1000; i < 1032; ++i) {
    ins[0] = (i / 24) % 2 == 0 ? 0.7f : -0.7f;
    a->process(ins, out_a);
    b->process(ins, out_b);
    CHECK(out_a[0] == doctest::Approx(out_b[0]));
  }
}
```

- [ ] **Step 5: Run tests** — Expected: PASS.
- [ ] **Step 6: Commit** — `git commit -m "feat: add ladder 4-pole ZDF lowpass module"`

---

### Task 9: lpg module (vactrol low pass gate)

**Files:**
- Create: `src/dsp/modules/lpg.cpp`
- Modify: `src/dsp/modules.h`, `src/dsp/registry.cpp`
- Test: `tests/test_lpg.cpp`, append two cases to `tests/test_migrate.cpp`

**Interfaces:**
- Produces: `lpg_info` — type `"lpg"`, inputs `in` (0), `cv` (0), `strike` (0), `damp` (0.5), `decay` (0.25), output `out`. Vactrol envelope `env_`: strike rising edge → `env_ = 1`; otherwise slews toward `clamp(cv,0,1)` — 2 ms attack when rising, `decay`-seconds exponential when falling. Two cascaded one-pole LPs whose cutoff is `30 * 2^(env*(9 - 6*clamp(damp,0,1)))` Hz (clamped to 0.45·fs), output scaled by `env_` (gate). State: `env_`, `lp1_`, `lp2_`, `prev_strike_`.

- [ ] **Step 1: Write the failing tests** — `tests/test_lpg.cpp`:

```cpp
#include <doctest/doctest.h>
#include <cmath>
#include "dsp/modules.h"

using namespace ms::dsp;

namespace {
struct LpgRig {
  std::unique_ptr<Module> module;
  // in, cv, strike, damp, decay
  float ins[5] = {0.0f, 0.0f, 0.0f, 0.5f, 0.05f};

  LpgRig() {
    const ModuleInfo* info = find_module("lpg");
    REQUIRE(info != nullptr);
    module = info->create();
    module->init(48000.0, {});
  }

  float tick() {
    float out[1];
    module->process(ins, out);
    return out[0];
  }
};

double rms_over(LpgRig& rig, int n, float freq) {
  double sum = 0.0;
  for (int i = 0; i < n; ++i) {
    rig.ins[0] = std::sin(static_cast<float>(2.0 * 3.14159265358979 * freq * i / 48000.0));
    sum += static_cast<double>(rig.tick()) * rig.tick();
  }
  return std::sqrt(sum / n);
}
}  // namespace

TEST_CASE("lpg is silent with no cv and no strike") {
  LpgRig lpg;
  for (int i = 0; i < 1000; ++i) {
    lpg.ins[0] = i % 2 == 0 ? 1.0f : -1.0f;
    CHECK(std::abs(lpg.tick()) < 1e-4f);
  }
}

TEST_CASE("lpg opens with cv and passes signal") {
  LpgRig lpg;
  lpg.ins[1] = 1.0f;  // cv high
  double sum = 0.0;
  for (int i = 0; i < 4800; ++i) {
    lpg.ins[0] = std::sin(static_cast<float>(2.0 * 3.14159265358979 * 220.0 * i / 48000.0));
    const float o = lpg.tick();
    if (i > 2400) sum += static_cast<double>(o) * o;
  }
  CHECK(std::sqrt(sum / 2400.0) > 0.3);
}

TEST_CASE("lpg strike plucks then decays back to silence") {
  LpgRig lpg;
  lpg.ins[4] = 0.02f;  // 20 ms decay
  // Feed a constant tone; strike once.
  auto energy = [&](int n) {
    double s = 0.0;
    for (int i = 0; i < n; ++i) {
      lpg.ins[0] = std::sin(static_cast<float>(2.0 * 3.14159265358979 * 220.0 * i / 48000.0));
      const float o = lpg.tick();
      s += static_cast<double>(o) * o;
    }
    return std::sqrt(s / n);
  };
  lpg.ins[2] = 1.0f;  // strike edge on first sample
  const double open = energy(480);   // first 10 ms: gate open
  lpg.ins[2] = 0.0f;
  (void)energy(9600);                // 200 ms: ten decay constants pass
  const double closed = energy(480);
  CHECK(open > 0.1);
  CHECK(closed < open * 0.01);
}

TEST_CASE("lpg damp darkens the output") {
  LpgRig bright;
  LpgRig dark;
  bright.ins[1] = dark.ins[1] = 1.0f;   // both fully open
  bright.ins[3] = 0.0f;
  dark.ins[3] = 1.0f;
  // High-frequency content comparison at 6 kHz.
  double sb = 0.0, sd = 0.0;
  for (int i = 0; i < 9600; ++i) {
    const float x = std::sin(static_cast<float>(2.0 * 3.14159265358979 * 6000.0 * i / 48000.0));
    bright.ins[0] = x;
    dark.ins[0] = x;
    const float ob = bright.tick();
    const float od = dark.tick();
    if (i > 4800) {
      sb += static_cast<double>(ob) * ob;
      sd += static_cast<double>(od) * od;
    }
  }
  CHECK(sd < sb * 0.25);
}
```

- [ ] **Step 2: Verify failure** — Expected: FAIL (unknown module).
- [ ] **Step 3: Implement** — `src/dsp/modules/lpg.cpp`:

```cpp
#include <algorithm>
#include <cmath>
#include <numbers>
#include "dsp/modules.h"

namespace ms::dsp {
namespace {

constexpr InputInfo kIns[] = {{"in", 0.f},
                              {"cv", 0.f},
                              {"strike", 0.f},
                              {"damp", 0.5f},
                              {"decay", 0.25f}};
constexpr OutputInfo kOuts[] = {{"out"}};

class Lpg final : public Module {
 public:
  void init(double rate, const Config&) override {
    rate_ = static_cast<float>(rate);
    env_ = 0.0f;
    lp1_ = lp2_ = 0.0f;
    prev_strike_ = 0.0f;
  }

  void process(const float* ins, float* outs) override {
    if (ins[2] > 0.5f && prev_strike_ <= 0.5f) env_ = 1.0f;
    prev_strike_ = ins[2];

    const float target = std::clamp(ins[1], 0.0f, 1.0f);
    if (target > env_) {
      const float a_att = 1.0f - std::exp(-1.0f / (0.002f * rate_));
      env_ += (target - env_) * a_att;
    } else {
      const float tau = std::max(ins[4], 0.001f);
      const float a_dec = 1.0f - std::exp(-1.0f / (tau * rate_));
      env_ += (target - env_) * a_dec;
    }

    const float damp = std::clamp(ins[3], 0.0f, 1.0f);
    const float fc = std::min(30.0f * std::exp2(env_ * (9.0f - 6.0f * damp)),
                              0.45f * rate_);
    const float a =
        1.0f - std::exp(-2.0f * std::numbers::pi_v<float> * fc / rate_);
    lp1_ += a * (ins[0] - lp1_);
    lp2_ += a * (lp1_ - lp2_);
    outs[0] = lp2_ * env_;
  }

  void copy_state_from(const Module& other) override {
    const auto& o = static_cast<const Lpg&>(other);
    env_ = o.env_;
    lp1_ = o.lp1_;
    lp2_ = o.lp2_;
    prev_strike_ = o.prev_strike_;
  }

 private:
  float rate_ = 48000.0f;
  float env_ = 0.0f;
  float lp1_ = 0.0f, lp2_ = 0.0f;
  float prev_strike_ = 0.0f;
};

std::unique_ptr<Module> make() { return std::make_unique<Lpg>(); }
}  // namespace

const ModuleInfo lpg_info{"lpg", kIns, {}, kOuts, &make};
}  // namespace ms::dsp
```

Extern + registry entry.

- [ ] **Step 4: Add load-bearing migration tests** — append to `tests/test_migrate.cpp`:

```cpp
TEST_CASE("lpg migrates envelope and filter state mid-pluck") {
  const ModuleInfo* info = find_module("lpg");
  REQUIRE(info != nullptr);
  auto a = info->create();
  auto b = info->create();
  a->init(48000.0, {});
  b->init(48000.0, {});
  float ins[5] = {0.0f, 0.0f, 0.0f, 0.5f, 0.1f};
  float out_a[1], out_b[1];
  ins[2] = 1.0f;  // strike
  a->process(ins, out_a);
  // Decay for a while with audio flowing so env_, lp1_, lp2_ are all nonzero.
  for (int i = 1; i < 400; ++i) {
    ins[0] = (i / 20) % 2 == 0 ? 0.6f : -0.6f;
    a->process(ins, out_a);  // strike stays high: prev_strike_ is 1.0
  }
  b->copy_state_from(*a);
  // Strike held high across the copy: a forgotten prev_strike_ re-plucks b.
  for (int i = 400; i < 432; ++i) {
    ins[0] = (i / 20) % 2 == 0 ? 0.6f : -0.6f;
    a->process(ins, out_a);
    b->process(ins, out_b);
    CHECK(out_a[0] == doctest::Approx(out_b[0]));
  }
}
```

- [ ] **Step 5: Run tests** — Expected: PASS. Sanity-check load-bearing: deleting any of the four copies in `copy_state_from` must fail this test (env_/prev_strike_ diverge via re-pluck; lp1_/lp2_ via the square wave).
- [ ] **Step 6: Commit** — `git commit -m "feat: add lpg vactrol low pass gate module"`

---

### Task 10: random module

**Files:**
- Create: `src/dsp/modules/random.cpp`
- Modify: `src/dsp/modules.h`, `src/dsp/registry.cpp`
- Test: `tests/test_random.cpp`, append one case to `tests/test_migrate.cpp`

**Interfaces:**
- Produces: `random_info` — type `"random"`, inputs `rate` (1), `smooth` (1), config `seed` (number, default 1), outputs `out`, `stepped`. Internal clock at `rate` Hz; each tick shifts `next_ -> prev_` and draws a new `next_` uniform in ±1 (xorshift32). `stepped` = `next_`; `out` crossfades between `stepped` and a cosine-eased interpolation from `prev_` to `next_` by `clamp(smooth,0,1)`. State: `state_`, `phase_`, `prev_`, `next_`.

- [ ] **Step 1: Write the failing tests** — `tests/test_random.cpp`:

```cpp
#include <doctest/doctest.h>
#include <cmath>
#include "dsp/modules.h"

using namespace ms::dsp;

namespace {
std::unique_ptr<Module> make_random(double seed) {
  const ModuleInfo* info = find_module("random");
  REQUIRE(info != nullptr);
  auto m = info->create();
  Config cfg;
  ConfigValue s; s.num = seed; s.is_num = true;
  cfg.push_back({"seed", s});
  m->init(48000.0, cfg);
  return m;
}
}  // namespace

TEST_CASE("random stepped output changes at the clock rate and stays bounded") {
  auto m = make_random(3.0);
  float ins[2] = {100.0f, 0.0f};  // 100 Hz, no smoothing
  float out[2];
  int changes = 0;
  float prev = 0.0f;
  for (int i = 0; i < 48000; ++i) {
    m->process(ins, out);
    CHECK(std::abs(out[1]) <= 1.0f);
    CHECK(out[0] == doctest::Approx(out[1]));  // smooth 0: out == stepped
    if (i > 0 && out[1] != prev) ++changes;
    prev = out[1];
  }
  CHECK(changes >= 95);
  CHECK(changes <= 105);
}

TEST_CASE("random is deterministic per seed") {
  auto a = make_random(5.0);
  auto b = make_random(5.0);
  auto c = make_random(6.0);
  float ins[2] = {500.0f, 0.0f};
  float oa[2], ob[2], oc[2];
  bool differs = false;
  for (int i = 0; i < 4800; ++i) {
    a->process(ins, oa);
    b->process(ins, ob);
    c->process(ins, oc);
    CHECK(oa[1] == ob[1]);
    if (oa[1] != oc[1]) differs = true;
  }
  CHECK(differs);
}

TEST_CASE("random smooth output is continuous") {
  auto m = make_random(9.0);
  float ins[2] = {50.0f, 1.0f};  // full smoothing
  float out[2];
  m->process(ins, out);
  float prev = out[0];
  float max_jump = 0.0f;
  for (int i = 1; i < 48000; ++i) {
    m->process(ins, out);
    max_jump = std::max(max_jump, std::abs(out[0] - prev));
    prev = out[0];
  }
  // 50 Hz segments at 48 kHz = 960 samples; a cosine ramp over ±2 range
  // moves at most ~2*pi/960 per sample. Allow generous margin.
  CHECK(max_jump < 0.02f);
}
```

- [ ] **Step 2: Verify failure** — Expected: FAIL (unknown module).
- [ ] **Step 3: Implement** — `src/dsp/modules/random.cpp`:

```cpp
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <numbers>
#include "dsp/modules.h"

namespace ms::dsp {
namespace {

constexpr InputInfo kIns[] = {{"rate", 1.f}, {"smooth", 1.f}};
constexpr ConfigInfo kCfg[] = {{"seed", nullptr}};
constexpr OutputInfo kOuts[] = {{"out"}, {"stepped"}};

class Random final : public Module {
 public:
  void init(double rate, const Config& cfg) override {
    rate_ = static_cast<float>(rate);
    const auto seed = static_cast<std::uint32_t>(cfg_num(cfg, "seed", 1.0));
    state_ = seed * 2654435761u;
    if (state_ == 0) state_ = 0x9E3779B9u;
    phase_ = 0.0f;
    prev_ = draw();
    next_ = draw();
  }

  void process(const float* ins, float* outs) override {
    phase_ += std::max(ins[0], 0.0f) / rate_;
    if (phase_ >= 1.0f) {
      phase_ -= std::floor(phase_);
      prev_ = next_;
      next_ = draw();
    }
    const float sm = std::clamp(ins[1], 0.0f, 1.0f);
    const float eased =
        prev_ + (next_ - prev_) *
                    0.5f * (1.0f - std::cos(std::numbers::pi_v<float> * phase_));
    outs[0] = next_ + (eased - next_) * sm;
    outs[1] = next_;
  }

  void copy_state_from(const Module& other) override {
    const auto& o = static_cast<const Random&>(other);
    state_ = o.state_;
    phase_ = o.phase_;
    prev_ = o.prev_;
    next_ = o.next_;
  }

 private:
  float draw() {
    std::uint32_t x = state_;
    x ^= x << 13;
    x ^= x >> 17;
    x ^= x << 5;
    state_ = x;
    return 2.0f * (static_cast<float>(x) * (1.0f / 4294967296.0f)) - 1.0f;
  }

  float rate_ = 48000.0f;
  std::uint32_t state_ = 0x9E3779B9u;
  float phase_ = 0.0f;
  float prev_ = 0.0f;
  float next_ = 0.0f;
};

std::unique_ptr<Module> make() { return std::make_unique<Random>(); }
}  // namespace

const ModuleInfo random_info{"random", kIns, kCfg, kOuts, &make};
}  // namespace ms::dsp
```

Extern + registry entry.

- [ ] **Step 4: Add load-bearing migration test** — append to `tests/test_migrate.cpp` (copy mid-segment with smoothing on: phase_, prev_, next_ all shape `out` immediately; run past the next clock tick so `state_` divergence also shows):

```cpp
TEST_CASE("random migrates rng, phase, and segment endpoints") {
  const ModuleInfo* info = find_module("random");
  REQUIRE(info != nullptr);
  auto a = info->create();
  auto b = info->create();
  Config cfg;
  ConfigValue s; s.num = 4.0; s.is_num = true;
  cfg.push_back({"seed", s});
  a->init(48000.0, cfg);
  b->init(48000.0, cfg);
  const float ins[2] = {200.0f, 1.0f};  // 240-sample segments, smoothing on
  float out_a[2], out_b[2];
  for (int i = 0; i < 500; ++i) a->process(ins, out_a);  // mid-segment
  b->copy_state_from(*a);
  for (int i = 0; i < 480; ++i) {  // crosses at least one clock tick
    a->process(ins, out_a);
    b->process(ins, out_b);
    CHECK(out_a[0] == doctest::Approx(out_b[0]));
    CHECK(out_a[1] == doctest::Approx(out_b[1]));
  }
}
```

- [ ] **Step 5: Run tests** — Expected: PASS.
- [ ] **Step 6: Commit** — `git commit -m "feat: add random wandering-voltage module"`

---

### Task 11: quant module

**Files:**
- Create: `src/dsp/modules/quant.cpp`
- Modify: `src/dsp/modules.h`, `src/dsp/registry.cpp`
- Test: `tests/test_quant.cpp`, append one case to `tests/test_migrate.cpp`

**Interfaces:**
- Produces: `quant_info` — type `"quant"`, inputs `in` (0, v_oct), `root` (0, semitones), `transpose` (0, v_oct, summed with `in` before quantizing), config `scale` (array of semitone degrees, empty/absent = chromatic), outputs `out` (v_oct), `changed` (1-sample gate when the quantized degree changes; no gate on the very first sample). Quantization: `semis = (in + transpose)*12 - root`, snap to the nearest allowed degree across octaves (ties snap down), `out = (snapped + root)/12`. State: `last_q_`, `first_`.

- [ ] **Step 1: Write the failing tests** — `tests/test_quant.cpp`:

```cpp
#include <doctest/doctest.h>
#include "dsp/modules.h"

using namespace ms::dsp;

namespace {
struct QuantRig {
  std::unique_ptr<Module> module;
  float ins[3] = {0.0f, 0.0f, 0.0f};  // in, root, transpose

  explicit QuantRig(std::vector<double> scale = {}) {
    const ModuleInfo* info = find_module("quant");
    REQUIRE(info != nullptr);
    module = info->create();
    Config cfg;
    if (!scale.empty()) {
      ConfigValue v; v.arr = std::move(scale);
      cfg.push_back({"scale", v});
    }
    module->init(48000.0, cfg);
  }

  void tick(float in, float (&out)[2]) {
    ins[0] = in;
    module->process(ins, out);
  }
};
}  // namespace

TEST_CASE("default chromatic scale rounds to the nearest semitone") {
  QuantRig q;
  float out[2];
  q.tick(0.26f, out);  // 3.12 semis -> 3 semis = 0.25 v_oct
  CHECK(out[0] == doctest::Approx(0.25f));
  q.tick(-1.0f, out);
  CHECK(out[0] == doctest::Approx(-1.0f));
}

TEST_CASE("scale array restricts degrees across octaves") {
  QuantRig q({0.0, 7.0});  // root + fifth
  float out[2];
  q.tick(2.0f / 12.0f, out);  // 2 semis: nearest of {0,7,-5,12} is 0
  CHECK(out[0] == doctest::Approx(0.0f));
  q.tick(6.0f / 12.0f, out);  // 6 semis: 7 is 1 away, 0 is 6 away
  CHECK(out[0] == doctest::Approx(7.0f / 12.0f));
  q.tick(11.0f / 12.0f, out);  // 11 semis: 12 (next octave root) wins
  CHECK(out[0] == doctest::Approx(1.0f));
  q.tick(-3.0f / 12.0f, out);  // -3 semis: -5 is 2 away, 0 is 3 away
  CHECK(out[0] == doctest::Approx(-5.0f / 12.0f));
}

TEST_CASE("root shifts the scale and transpose sums pre-quantize") {
  QuantRig q({0.0, 4.0, 7.0});  // major triad
  q.ins[1] = 2.0f;              // root = D
  float out[2];
  q.tick(0.0f, out);  // 0 semis, scale-relative -2 -> nearest degree 0 -> +root
  CHECK(out[0] == doctest::Approx(2.0f / 12.0f));
  q.ins[2] = 1.0f;  // transpose up an octave
  q.tick(0.0f, out);
  CHECK(out[0] == doctest::Approx(14.0f / 12.0f));
}

TEST_CASE("changed emits a one-sample gate per new degree, none at start") {
  QuantRig q;
  float out[2];
  q.tick(0.0f, out);
  CHECK(out[1] == doctest::Approx(0.0f));  // first sample: no gate
  q.tick(0.0f, out);
  CHECK(out[1] == doctest::Approx(0.0f));  // unchanged
  q.tick(0.25f, out);                      // new degree
  CHECK(out[1] == doctest::Approx(1.0f));
  q.tick(0.25f, out);
  CHECK(out[1] == doctest::Approx(0.0f));  // gate is one sample wide
}
```

- [ ] **Step 2: Verify failure** — Expected: FAIL (unknown module).
- [ ] **Step 3: Implement** — `src/dsp/modules/quant.cpp`:

```cpp
#include <cmath>
#include <vector>
#include "dsp/modules.h"

namespace ms::dsp {
namespace {

constexpr InputInfo kIns[] = {{"in", 0.f}, {"root", 0.f}, {"transpose", 0.f}};
constexpr ConfigInfo kCfg[] = {{"scale", nullptr}};
constexpr OutputInfo kOuts[] = {{"out"}, {"changed"}};

class Quant final : public Module {
 public:
  void init(double, const Config& cfg) override {
    degrees_.clear();
    bool present[12] = {};
    const auto& arr = cfg_arr(cfg, "scale");
    if (arr.empty()) {
      for (int i = 0; i < 12; ++i) present[i] = true;
    } else {
      for (double d : arr) {
        int s = static_cast<int>(std::lround(d)) % 12;
        if (s < 0) s += 12;
        present[s] = true;
      }
    }
    for (int i = 0; i < 12; ++i)
      if (present[i]) degrees_.push_back(i);
    first_ = true;
    last_q_ = 0;
  }

  void process(const float* ins, float* outs) override {
    const float semis = (ins[0] + ins[2]) * 12.0f - ins[1];
    const int base =
        static_cast<int>(std::floor(semis / 12.0f)) * 12;
    int best = base + degrees_.front();
    float bestd = 1e9f;
    for (int oct = base - 12; oct <= base + 12; oct += 12) {
      for (int d : degrees_) {
        const float dist = std::abs(static_cast<float>(oct + d) - semis);
        if (dist < bestd) {  // strict <: ascending scan means ties snap down
          bestd = dist;
          best = oct + d;
        }
      }
    }
    outs[0] = (static_cast<float>(best) + ins[1]) / 12.0f;
    outs[1] = (!first_ && best != last_q_) ? 1.0f : 0.0f;
    last_q_ = best;
    first_ = false;
  }

  void copy_state_from(const Module& other) override {
    const auto& o = static_cast<const Quant&>(other);
    last_q_ = o.last_q_;
    first_ = o.first_;
  }

 private:
  std::vector<int> degrees_;  // built in init (non-RT); read-only in process
  int last_q_ = 0;
  bool first_ = true;
};

std::unique_ptr<Module> make() { return std::make_unique<Quant>(); }
}  // namespace

const ModuleInfo quant_info{"quant", kIns, kCfg, kOuts, &make};
}  // namespace ms::dsp
```

Extern + registry entry.

- [ ] **Step 4: Add load-bearing migration test** — append to `tests/test_migrate.cpp` (a forgotten `first_`/`last_q_` copy makes `b` either suppress or spuriously fire `changed` right after the copy):

```cpp
TEST_CASE("quant migrates changed-gate state") {
  const ModuleInfo* info = find_module("quant");
  REQUIRE(info != nullptr);
  auto a = info->create();
  auto b = info->create();
  a->init(48000.0, {});
  b->init(48000.0, {});
  float ins[3] = {0.25f, 0.0f, 0.0f};
  float out_a[2], out_b[2];
  a->process(ins, out_a);  // a: first_ consumed, last_q_ = 3
  b->copy_state_from(*a);
  // Change the degree on the first post-copy sample: `a` fires changed
  // because last_q_ was 3; a fresh `b` (first_ true) would stay silent.
  ins[0] = 0.5f;
  a->process(ins, out_a);
  b->process(ins, out_b);
  CHECK(out_a[1] == doctest::Approx(1.0f));
  CHECK(out_b[1] == doctest::Approx(out_a[1]));
  for (int i = 0; i < 8; ++i) {
    ins[0] = i % 2 == 0 ? 0.25f : 0.5f;
    a->process(ins, out_a);
    b->process(ins, out_b);
    CHECK(out_a[0] == doctest::Approx(out_b[0]));
    CHECK(out_a[1] == doctest::Approx(out_b[1]));
  }
}
```

- [ ] **Step 5: Run tests** — Expected: PASS.
- [ ] **Step 6: Commit** — `git commit -m "feat: add quant pitch quantizer module"`

---

### Task 12: mix module

**Files:**
- Create: `src/dsp/modules/mix.cpp`
- Modify: `src/dsp/modules.h`, `src/dsp/registry.cpp`
- Test: `tests/test_mix.cpp`

**Interfaces:**
- Produces: `mix_info` — type `"mix"`, inputs `in1..in8` (0) then `lvl1..lvl8` (0), configs `levels` (array, per-channel base gain, default 1) and `pans` (array, −1 left .. +1 right, default 0 center), outputs `out_l`, `out_r`. Per channel: `gain = clamp(levels[i] + lvl_i, 0, 8)`; equal-power pan gains precomputed in `init` (pans are config-only): `angle = (pan+1)*pi/4`, `gl = cos`, `gr = sin`. Stateless at runtime.

- [ ] **Step 1: Write the failing tests** — `tests/test_mix.cpp`:

```cpp
#include <doctest/doctest.h>
#include <cmath>
#include "dsp/modules.h"

using namespace ms::dsp;

namespace {
std::unique_ptr<Module> make_mix(std::vector<double> levels = {},
                                 std::vector<double> pans = {}) {
  const ModuleInfo* info = find_module("mix");
  REQUIRE(info != nullptr);
  auto m = info->create();
  Config cfg;
  if (!levels.empty()) {
    ConfigValue v; v.arr = std::move(levels);
    cfg.push_back({"levels", v});
  }
  if (!pans.empty()) {
    ConfigValue v; v.arr = std::move(pans);
    cfg.push_back({"pans", v});
  }
  m->init(48000.0, cfg);
  return m;
}
}  // namespace

TEST_CASE("mix exposes eight signal and eight level inputs") {
  const ModuleInfo* info = find_module("mix");
  REQUIRE(info != nullptr);
  CHECK(info->input_index("in1") == 0);
  CHECK(info->input_index("in8") == 7);
  CHECK(info->input_index("lvl1") == 8);
  CHECK(info->input_index("lvl8") == 15);
  CHECK(info->output_index("out_l") == 0);
  CHECK(info->output_index("out_r") == 1);
}

TEST_CASE("mix centers a single input at equal power") {
  auto m = make_mix();
  float ins[16] = {};
  ins[0] = 1.0f;
  float out[2];
  m->process(ins, out);
  CHECK(out[0] == doctest::Approx(std::sqrt(0.5f)).epsilon(0.001));
  CHECK(out[1] == doctest::Approx(std::sqrt(0.5f)).epsilon(0.001));
}

TEST_CASE("mix pans hard left and right") {
  auto m = make_mix({1.0, 1.0}, {-1.0, 1.0});
  float ins[16] = {};
  ins[0] = 0.5f;  // hard left
  ins[1] = 0.25f; // hard right
  float out[2];
  m->process(ins, out);
  CHECK(out[0] == doctest::Approx(0.5f).epsilon(0.001));
  CHECK(out[1] == doctest::Approx(0.25f).epsilon(0.001));
}

TEST_CASE("mix levels config sets base gains and lvl inputs modulate") {
  auto m = make_mix({0.5});
  float ins[16] = {};
  ins[0] = 1.0f;
  ins[8] = 0.25f;  // lvl1 sums onto the 0.5 base
  float out[2];
  m->process(ins, out);
  CHECK(out[0] == doctest::Approx(0.75f * std::sqrt(0.5f)).epsilon(0.001));
}

TEST_CASE("mix clamps negative summed gains to zero") {
  auto m = make_mix({0.5});
  float ins[16] = {};
  ins[0] = 1.0f;
  ins[8] = -2.0f;
  float out[2];
  m->process(ins, out);
  CHECK(out[0] == doctest::Approx(0.0f));
  CHECK(out[1] == doctest::Approx(0.0f));
}
```

- [ ] **Step 2: Verify failure** — Expected: FAIL (unknown module).
- [ ] **Step 3: Implement** — `src/dsp/modules/mix.cpp`:

```cpp
#include <algorithm>
#include <cmath>
#include <numbers>
#include "dsp/modules.h"

namespace ms::dsp {
namespace {

constexpr InputInfo kIns[] = {
    {"in1", 0.f}, {"in2", 0.f}, {"in3", 0.f}, {"in4", 0.f},
    {"in5", 0.f}, {"in6", 0.f}, {"in7", 0.f}, {"in8", 0.f},
    {"lvl1", 0.f}, {"lvl2", 0.f}, {"lvl3", 0.f}, {"lvl4", 0.f},
    {"lvl5", 0.f}, {"lvl6", 0.f}, {"lvl7", 0.f}, {"lvl8", 0.f}};
constexpr ConfigInfo kCfg[] = {{"levels", nullptr}, {"pans", nullptr}};
constexpr OutputInfo kOuts[] = {{"out_l"}, {"out_r"}};
constexpr int kCh = 8;

class Mix final : public Module {
 public:
  void init(double, const Config& cfg) override {
    const auto& levels = cfg_arr(cfg, "levels");
    const auto& pans = cfg_arr(cfg, "pans");
    for (int i = 0; i < kCh; ++i) {
      base_[i] = i < static_cast<int>(levels.size())
                     ? static_cast<float>(levels[static_cast<size_t>(i)])
                     : 1.0f;
      const float pan =
          i < static_cast<int>(pans.size())
              ? std::clamp(static_cast<float>(pans[static_cast<size_t>(i)]),
                           -1.0f, 1.0f)
              : 0.0f;
      const float angle = (pan + 1.0f) * (std::numbers::pi_v<float> / 4.0f);
      gl_[i] = std::cos(angle);
      gr_[i] = std::sin(angle);
    }
  }

  void process(const float* ins, float* outs) override {
    float l = 0.0f, r = 0.0f;
    for (int i = 0; i < kCh; ++i) {
      const float g = std::clamp(base_[i] + ins[kCh + i], 0.0f, 8.0f);
      const float v = ins[i] * g;
      l += v * gl_[i];
      r += v * gr_[i];
    }
    outs[0] = l;
    outs[1] = r;
  }

 private:
  float base_[kCh] = {};
  float gl_[kCh] = {};
  float gr_[kCh] = {};
};

std::unique_ptr<Module> make() { return std::make_unique<Mix>(); }
}  // namespace

const ModuleInfo mix_info{"mix", kIns, kCfg, kOuts, &make};
}  // namespace ms::dsp
```

Extern + registry entry.

- [ ] **Step 4: Run tests** — Expected: PASS.
- [ ] **Step 5: Commit** — `git commit -m "feat: add mix 8-channel stereo mixer module"`

---

### Task 13: scope module

**Files:**
- Create: `src/dsp/modules/scope.cpp`
- Modify: `src/dsp/modules.h`, `src/dsp/registry.cpp`
- Test: `tests/test_scope.cpp`

**Interfaces:**
- Produces: `scope_info` — type `"scope"`, input `in` (0), config `label` (free string), outputs: none. Records a decaying peak meter and the last sample into members for the future TUI (spec: "probe/meter for TUI & logs"); no I/O from `process()`. A patch can declare `probe = scope { label: melody }` and cable into it today; readout wiring is a later phase.

- [ ] **Step 1: Write the failing tests** — `tests/test_scope.cpp`:

```cpp
#include <doctest/doctest.h>
#include "dsp/modules.h"

using namespace ms::dsp;

TEST_CASE("scope registers with one input, a label config, and no outputs") {
  const ModuleInfo* info = find_module("scope");
  REQUIRE(info != nullptr);
  CHECK(info->input_index("in") == 0);
  CHECK(info->outputs.empty());
  CHECK(info->config("label") != nullptr);
}

TEST_CASE("scope processes samples without touching outputs") {
  const ModuleInfo* info = find_module("scope");
  REQUIRE(info != nullptr);
  auto m = info->create();
  Config cfg;
  ConfigValue v; v.str = "melody";
  cfg.push_back({"label", v});
  m->init(48000.0, cfg);
  const float ins[1] = {0.7f};
  float sentinel[1] = {123.0f};
  for (int i = 0; i < 100; ++i) m->process(ins, sentinel);
  CHECK(sentinel[0] == doctest::Approx(123.0f));  // zero outputs: untouched
}
```

- [ ] **Step 2: Verify failure** — Expected: FAIL (unknown module).
- [ ] **Step 3: Implement** — `src/dsp/modules/scope.cpp`:

```cpp
#include <algorithm>
#include <cmath>
#include <string>
#include "dsp/modules.h"

namespace ms::dsp {
namespace {

constexpr InputInfo kIns[] = {{"in", 0.f}};
constexpr ConfigInfo kCfg[] = {{"label", nullptr}};

class Scope final : public Module {
 public:
  void init(double, const Config& cfg) override {
    label_ = cfg_str(cfg, "label", "");
    peak_ = 0.0f;
    last_ = 0.0f;
  }

  void process(const float* ins, float*) override {
    last_ = ins[0];
    peak_ = std::max(peak_ * 0.9995f, std::abs(ins[0]));
  }

  void copy_state_from(const Module& other) override {
    const auto& o = static_cast<const Scope&>(other);
    peak_ = o.peak_;
    last_ = o.last_;
  }

 private:
  std::string label_;  // set in init (non-RT); read by the future TUI
  float peak_ = 0.0f;
  float last_ = 0.0f;
};

std::unique_ptr<Module> make() { return std::make_unique<Scope>(); }
}  // namespace

const ModuleInfo scope_info{"scope", kIns, kCfg, {}, &make};
}  // namespace ms::dsp
```

Extern + registry entry.

- [ ] **Step 4: Run tests** — Expected: PASS (also confirms the engine tolerates a zero-output node — if `mstests` or `cli_check` breaks here, the engine's per-node output loop needs a look before proceeding).
- [ ] **Step 5: Commit** — `git commit -m "feat: add scope probe module"`

---

### Task 14: west-coast demo patch + full verification

**Files:**
- Create: `patches/west-coast.patch`
- Test: existing CLI gates (`modsynth check`, `mise run render`)

**Interfaces:**
- Consumes: every module from Tasks 2–13 plus existing clock/div/func/vco.

- [ ] **Step 1: Write the patch** — `patches/west-coast.patch`:

```
# west coast -- random/quantized melody, folded triangle, vactrol plucks.
# Exercises: random, quant, fold, lpg, drive, noise, svf, sh, xfade, math, mix, scope.

clk = clock { bpm: 112 }
half = div { by: 2 }
clk -> half.in

# -- melody: wandering voltage, sampled per step, snapped to a minor pentatonic --
wander = random { rate: 7.3, seed: 11, smooth: 1 }
stepper = sh { mode: sample }
wander -> stepper.in
clk -> stepper.trig
qnt = quant { scale: [0, 3, 5, 7, 10] }
stepper -> qnt.in : 0.4
probe = scope { label: melody }
qnt -> probe.in

# -- voice: triangle into a wavefolder, fold depth breathes slowly --
osc = vco { freq: 220hz, wave: tri }
qnt -> osc.v_oct
breathe = lfo { wave: sine, freq: 0.045hz }
folder = fold { fold: 1.6 }
osc -> folder.in
breathe -> folder.fold : 1.2

# -- vactrol pluck on every other step --
gate = lpg { damp: 0.35, decay: 0.3 }
folder -> gate.in
half -> gate.strike

# -- texture: pink noise, brightened by ring-modding with the raw osc --
hiss = noise { color: pink, seed: 3 }
shimmer = math { op: mult }
hiss -> shimmer.a
osc -> shimmer.b : 0.6
bed = xfade { mix: 0.25 }
hiss -> bed.a : 0.2
shimmer -> bed.b : 0.5

# -- gentle glue drive on the voice --
warm = drive { amount: 0.15, mode: soft }
gate -> warm.in

# -- final mix: voice center-left, texture wide right --
mixer = mix { levels: [0.9, 0.35], pans: [-0.2, 0.55] }
warm -> mixer.in1
bed -> mixer.in2
mixer.out_l -> out.left
mixer.out_r -> out.right
-0.15 -> out.level
```

- [ ] **Step 2: Validate** — `mise run build && ./build/modsynth check patches/west-coast.patch` — Expected: `ok`.
- [ ] **Step 3: Render** — `mise run render -- patches/west-coast.patch -d 5s -o /tmp/west-coast.wav` — Expected: exits 0, wav is nonzero size and contains audio (not digital silence): `python3 -c "import wave, audioop, sys; w = wave.open('/tmp/west-coast.wav'); f = w.readframes(w.getnframes()); print(audioop.rms(f, w.getsampwidth()))"` prints > 100.
- [ ] **Step 4: Full gates** — `mise run test && mise run test-san && mise run ci` — Expected: all pass.
- [ ] **Step 5: Commit** — `git add patches/west-coast.patch && git commit -m "feat: add west-coast demo patch exercising the full phase 1 palette"`

---

## Self-Review Notes

- **Spec coverage:** all 12 unimplemented Phase 1 rows have a task; the only spec-table edit is noise's added `seed` (Task 2, same commit). `quant.transpose` is defined in v_oct (spec table doesn't state units; §4 pitch convention applied). `scope` records internally with no readout — matches "probe/meter for TUI & logs" at Phase 1 scope.
- **Type consistency:** all modules use `float` state except where accumulation precision demands otherwise (none here — no long-horizon accumulators; `random.phase_` wraps every segment so float is safe, unlike func's fixed bug).
- **Placeholder scan:** every step carries complete code/commands; registry/extern edits are one-liners named per task.
- **Known risk:** Task 13 is the first zero-output module; its Step 4 explicitly checks the engine tolerates it. Task 12 is the first 16-input module; the engine sizes its scratch from the registry max, so no edit expected.
