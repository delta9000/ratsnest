# FM waveforms + keyboard scaling Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking; a task is done only when its named tests pass.

**Goal:** Add per-operator waveforms and DX-style keyboard scaling to `fm`, plus a small general DSL enabler (bareword arrays), per `docs/superpowers/specs/2026-07-18-fm-waveforms-keyscale-design.md`.

**Architecture:** Component A is an additive change to config lowering (`ConfigValue.strs` + a `cfg_str_arr` reader + kind-aware `to_config_value`); no lexer/parser/AST edits. Components B and C are `fm.cpp`-local: five DC-centered operator wave functions selected by a per-op `waves` bareword array, and per-op level/envelope-rate scaling latched from `v_oct` at the gate rising edge. Every addition defaults to a no-op so pre-existing patches render bit-identically.

**Tech Stack:** C++20, doctest, CMake presets via mise. Sources/tests are CMake-globbed — new files need no CMakeLists edits.

## Global Constraints

- `Module::process()` runs once per sample; it must never allocate, lock, or do I/O (design rule 3). Allocation happens in `init()` only. Wave functions and scaling are pure and allocation-free.
- `copy_state_from()` is RT-called: bounded copies, no allocation. New migratable state: latched `level_scale[6]`, `time_scale[6]`.
- NaN-proof clamps use `!(x >= lo)` (the `clampn`/`clampd` helpers already in `fm.cpp`); `v_oct` clamps to −24..24 before the scaling latch.
- Double accumulation for envelope progress/phase is unchanged.
- `-Wall -Wextra -Wconversion` on; `mise run ci` is warnings-as-errors: cast `double`↔`float` explicitly.
- **Bit-identical defaults:** `waves` absent → the sine path uses `std::sin(A)` verbatim (not routed through `frac`); `key_*` default 0 → `level_scale`/`time_scale` are exactly `1.0`. Dedicated tests assert this.
- **Clean-room / licensing (spec §7):** implement all five waves from their formulas; use **no** Yamaha ROM/waveform tables, parameter data, or code, and **no** code/tables/curve data from GPL or other-licensed FM emulators or ROM-dump projects (e.g. Dexed / MSFA). Config values are generic (`sine`/`half`/`abs`/`quarter`/`fold`); no trademarks as feature names or in output. No new dependency; `THIRD_PARTY_NOTICES.md` untouched.
- Build/test: `mise run build`; one file: `cmake --build build -j && ./build/mstests -sf="*test_fm*"`; gates `mise run test`, `mise run test-san`, `mise run ci`.
- Commits: conventional prefixes, tool-agnostic, no AI attribution.

---

### Task 1: DSL bareword arrays (Component A)

`ConfigValue` carries string arrays; `to_config_value` lowers `Ident`/`Str` array items into them; a `cfg_str_arr` reader exposes them. Numeric arrays are untouched.

**Files:**
- Modify: `src/dsp/module.h` (add `strs` to `ConfigValue`; declare `cfg_str_arr`)
- Modify: `src/dsp/module.cpp` (define `cfg_str_arr`)
- Modify: `src/graph/graph.cpp` (`to_config_value` kind-aware array lowering)
- Test: `tests/test_graph.cpp`

**Interfaces:**
- Produces: `struct ConfigValue { …; std::vector<std::string> strs; };` and
  `const std::vector<std::string>& ms::dsp::cfg_str_arr(const Config&, std::string_view);`

- [ ] **Step 1: Write the failing test**

Add to `tests/test_graph.cpp` (it already has `#include "dsl/parser.h"`, `#include "graph/graph.h"`, `using namespace ms;`, and a `go(src)` helper):

```cpp
TEST_CASE("bareword arrays lower to strs; numeric arrays stay numeric") {
  auto r = go("q = quant { scale: [sine, abs, fold] }\nq -> out.in\n");
  REQUIRE(r.ok());
  int qi = -1;
  for (int i = 0; i < (int)r.graph->nodes.size(); ++i)
    if (r.graph->nodes[i].name == "q") qi = i;
  REQUIRE(qi >= 0);
  const auto& strs = dsp::cfg_str_arr(r.graph->nodes[qi].config, "scale");
  REQUIRE(strs.size() == 3);
  CHECK(strs[0] == "sine");
  CHECK(strs[1] == "abs");
  CHECK(strs[2] == "fold");
  CHECK(dsp::cfg_arr(r.graph->nodes[qi].config, "scale").empty());

  auto r2 = go("p = quant { scale: [0, 3, 5] }\np -> out.in\n");
  REQUIRE(r2.ok());
  int pi = -1;
  for (int i = 0; i < (int)r2.graph->nodes.size(); ++i)
    if (r2.graph->nodes[i].name == "p") pi = i;
  REQUIRE(pi >= 0);
  const auto& a = dsp::cfg_arr(r2.graph->nodes[pi].config, "scale");
  REQUIRE(a.size() == 3);
  CHECK(a[0] == 0.0);
  CHECK(a[2] == 5.0);
  CHECK(dsp::cfg_str_arr(r2.graph->nodes[pi].config, "scale").empty());
}
```

- [ ] **Step 2: Run to verify it fails**

Run: `cmake --build build -j 2>&1 | tail` — expect a compile error (`cfg_str_arr` / `strs` unknown).

- [ ] **Step 3: Implement**

In `src/dsp/module.h`, add a field to `ConfigValue` (after `arr`):

```cpp
  std::vector<double> arr;
  std::vector<std::string> strs;
  bool is_num = false;
```

Declare next to `cfg_arr`:

```cpp
const std::vector<std::string>& cfg_str_arr(const Config& c, std::string_view name);
```

In `src/dsp/module.cpp`, after `cfg_arr`:

```cpp
const std::vector<std::string>& cfg_str_arr(const Config& c, std::string_view name) {
  static const std::vector<std::string> kEmpty;
  for (const auto& [k, v] : c)
    if (k == name) return v.strs;
  return kEmpty;
}
```

In `src/graph/graph.cpp`, replace the `Array` case of `to_config_value`:

```cpp
    case dsl::Value::Kind::Array:
      for (const auto& item : v.items) {
        if (item.kind == dsl::Value::Kind::Num) cv.arr.push_back(item.num);
        else cv.strs.push_back(item.str);  // Ident or Str
      }
      break;
```

- [ ] **Step 4: Run the tests to verify they pass**

Run: `cmake --build build -j && ./build/mstests -sf="*bareword*"` — expect PASS. Also run the full suite once: `ctest --test-dir build --output-on-failure -R "graph|dsl" ` stays green.

- [ ] **Step 5: Commit**

```bash
git add src/dsp/module.h src/dsp/module.cpp src/graph/graph.cpp tests/test_graph.cpp
git commit -m "feat: DSL bareword arrays lower to a ConfigValue string array"
```

---

### Task 2: Operator waveforms (Component B)

Five DC-centered wave functions selected per op by `waves`, replacing the hardwired sine. Sine path stays bit-identical.

**Files:**
- Modify: `src/dsp/modules/fm.cpp`
- Test: `tests/test_fm.cpp`

**Interfaces:**
- Consumes: `dsp::cfg_str_arr` (Task 1).
- Produces: config `waves` (bareword array); five wave codes `sine`=0,`half`=1,`abs`=2,`quarter`=3,`fold`=4.

- [ ] **Step 1: Write the failing tests**

Add near the top of `tests/test_fm.cpp` (after the existing `arr`/`str` helpers) a string-array helper:

```cpp
ConfigValue warr(std::initializer_list<const char*> v) {
  ConfigValue c;
  for (const char* s : v) c.strs.emplace_back(s);
  return c;
}
```

Update the port-surface case to assert the new configs exist (add these lines inside `TEST_CASE("fm: registered with the full port surface")`):

```cpp
  CHECK(info->config("waves") != nullptr);
  CHECK(info->config("key_break") != nullptr);
  CHECK(info->config("key_level") != nullptr);
  CHECK(info->config("key_rate") != nullptr);
```

Add cases (uses the existing `dft_mag` helper — defined later in the file in the same TU, so these cases must be placed AFTER the `namespace { … dft_mag … }` block; put them at end of file):

```cpp
TEST_CASE("fm: abs waveform is octave-up (energy at 2x, none at fundamental) and DC-free") {
  Config cfg;
  cfg.push_back({"waves", warr({"abs"})});
  cfg.push_back({"t1", arr({0})}); cfg.push_back({"t2", arr({0})}); cfg.push_back({"t3", arr({0})});
  cfg.push_back({"l2", arr({1})}); cfg.push_back({"l3", arr({1})});
  FmRig rig(48000.0, cfg);
  rig.ins[1] = 480.0f;
  const int N = 48000;
  std::vector<float> x(static_cast<size_t>(N));
  for (int i = 0; i < N; ++i) { rig.tick(1.0f); x[static_cast<size_t>(i)] = rig.outs[1]; }
  CHECK(dft_mag(x, 960.0, 48000.0) > 0.1);     // strong 2x
  CHECK(dft_mag(x, 480.0, 48000.0) < 0.02);    // full-rectified: no fundamental
  CHECK(dft_mag(x, 0.0, 48000.0) < 0.02);      // DC ~ 0 after centering (2/N*|sum|)
}

TEST_CASE("fm: each non-sine waveform is denser than sine and DC-free") {
  for (const char* w : {"half", "quarter", "fold"}) {
    CAPTURE(w);
    Config cfg;
    cfg.push_back({"waves", warr({w})});
    cfg.push_back({"t1", arr({0})}); cfg.push_back({"t2", arr({0})}); cfg.push_back({"t3", arr({0})});
    cfg.push_back({"l2", arr({1})}); cfg.push_back({"l3", arr({1})});
    FmRig rig(48000.0, cfg);
    rig.ins[1] = 480.0f;
    const int N = 48000;
    std::vector<float> x(static_cast<size_t>(N));
    for (int i = 0; i < N; ++i) { rig.tick(1.0f); x[static_cast<size_t>(i)] = rig.outs[1]; }
    CHECK(dft_mag(x, 960.0, 48000.0) > 0.02);   // harmonic content above the fundamental
    CHECK(dft_mag(x, 0.0, 48000.0) < 0.03);     // DC-centered
    CHECK(std::isfinite(dft_mag(x, 1440.0, 48000.0)));
  }
}

TEST_CASE("fm: waves default and unknown names are sine, bit-identical") {
  Config base;                                   // no waves
  Config allsine;
  allsine.push_back({"waves", warr({"sine","sine","sine","sine","sine","sine"})});
  Config unknown;
  unknown.push_back({"waves", warr({"wobble"})}); // unknown -> sine
  FmRig rb(48000.0, base), rs(48000.0, allsine), ru(48000.0, unknown);
  for (int i = 0; i < 4000; ++i) {
    const float g = (i % 500) < 400 ? 1.0f : 0.0f;
    rb.tick(g); rs.tick(g); ru.tick(g);
    for (int o = 0; o < 7; ++o) {
      CHECK(rs.outs[o] == rb.outs[o]);
      CHECK(ru.outs[o] == rb.outs[o]);
    }
  }
}
```

- [ ] **Step 2: Run to verify they fail**

Run: `cmake --build build -j && ./build/mstests -sf="*test_fm*"` — expect FAIL (`waves` config unknown → `info->config("waves")` null; abs case fails, defaults trivially pass).

- [ ] **Step 3: Implement**

In `fm.cpp`, add the header-comment clean-room line (append to the existing top comment):

```cpp
// Operator waveforms (rectified/windowed/folded sine) are the TX81Z/DX11/FS1R
// operator-waveform *concept*, implemented locally from the mathematics; no
// Yamaha tables/code and no GPL/emulator code are used. See docs/attribution.md.
```

Add to `kCfg` (append entries):

```cpp
    {"waves", nullptr},   {"key_break", nullptr},
    {"key_level", nullptr}, {"key_rate", nullptr}};
```

Add the wave-name table near `kCurves`:

```cpp
constexpr const char* kWaves[] = {"sine", "half", "abs", "quarter", "fold", nullptr};
```

Add the wave function in the anonymous namespace (after `env_shape`), using `std::numbers::pi_v<float>`:

```cpp
// θ in [0,1). Each wave is DC-centered (one-period mean subtracted) so
// rectified shapes add no DC to carriers and no constant phase to modulators.
float op_wave(int w, float th) {
  const float pi = std::numbers::pi_v<float>;
  switch (w) {
    case 1: return (th < 0.5f ? std::sin(kTwoPi * th) : 0.0f) - 1.0f / pi;      // half
    case 2: return std::fabs(std::sin(kTwoPi * th)) - 2.0f / pi;               // abs
    case 3: return (th < 0.25f ? std::sin(2.0f * kTwoPi * th) : 0.0f)
                   - 1.0f / (2.0f * pi);                                       // quarter
    case 4: {                                                                  // fold
      const float x = 2.0f * std::sin(kTwoPi * th);       // drive 2x
      return (2.0f / pi) * std::asin(std::sin(0.5f * pi * x));  // triangle wavefold
    }
    default: return std::sin(kTwoPi * th);                                     // sine
  }
}
```

Add member: `std::array<int, kOps> waves_{};` (with the other config arrays).

In `init()`, after the envelope-config loop, map names → codes:

```cpp
    const auto& wv = cfg_str_arr(cfg, "waves");
    for (int k = 0; k < kOps; ++k) {
      waves_[static_cast<size_t>(k)] = 0;  // sine (also the unknown-name fallback)
      if (k < static_cast<int>(wv.size()))
        for (int i = 0; kWaves[i]; ++i)
          if (wv[static_cast<size_t>(k)] == kWaves[i]) waves_[static_cast<size_t>(k)] = i;
    }
```

In `process()`, replace the sine line

```cpp
      const float s =
          std::sin(kTwoPi * static_cast<float>(phase_[static_cast<size_t>(k)]) +
                   kIndex * static_cast<float>(mod));
```

with:

```cpp
      const float A = kTwoPi * static_cast<float>(phase_[static_cast<size_t>(k)]) +
                      kIndex * static_cast<float>(mod);
      float s;
      if (waves_[static_cast<size_t>(k)] == 0) {
        s = std::sin(A);  // bit-identical to the pre-waveform path
      } else {
        float th = A * (0.5f / std::numbers::pi_v<float>);  // A / 2π, in cycles
        th -= std::floor(th);
        s = op_wave(waves_[static_cast<size_t>(k)], th);
      }
```

- [ ] **Step 4: Run the tests to verify they pass**

Run: `cmake --build build -j && ./build/mstests -sf="*test_fm*"` — expect all `fm:` cases PASS. (If a spectral bound is slightly off against real output, adjust the numeric threshold — never weaken the DC/identity assertions.)

- [ ] **Step 5: Commit**

```bash
git add src/dsp/modules/fm.cpp tests/test_fm.cpp
git commit -m "feat: fm per-operator waveforms (sine/half/abs/quarter/fold)"
```

---

### Task 3: Keyboard scaling (Component C)

Per-op level and envelope-rate scaling, latched from `v_oct` at the gate rising edge; defaults are no-ops.

**Files:**
- Modify: `src/dsp/modules/fm.cpp`
- Test: `tests/test_fm.cpp`

**Interfaces:**
- Produces: configs `key_break` (num scalar), `key_level[6]`, `key_rate[6]`; migratable `level_scale[6]`, `time_scale[6]`.

- [ ] **Step 1: Write the failing tests**

Add a numeric-scalar config helper near `warr`:

```cpp
ConfigValue cnum(double x) { ConfigValue c; c.is_num = true; c.num = x; return c; }
```

Add cases at end of file (pitch pinned with `fixed` so `v_oct` drives *only* the scaling, never the audible frequency):

```cpp
TEST_CASE("fm: key_level scales op level by 2^(exp*(voct-break))") {
  Config cfg;
  cfg.push_back({"key_level", arr({-1})});     // op1 halves per octave up
  cfg.push_back({"fixed", arr({300})});        // pin pitch; voct feeds scaling only
  cfg.push_back({"t1", arr({0})}); cfg.push_back({"t2", arr({0})}); cfg.push_back({"t3", arr({0})});
  cfg.push_back({"l2", arr({1})}); cfg.push_back({"l3", arr({1})});
  auto rms = [&](float voct) {
    FmRig rig(48000.0, cfg);
    rig.ins[2] = voct;
    double s2 = 0.0;
    for (int i = 0; i < 48000; ++i) { rig.tick(1.0f); s2 += static_cast<double>(rig.outs[1]) * rig.outs[1]; }
    return std::sqrt(s2 / 48000.0);
  };
  CHECK(rms(1.0f) == doctest::Approx(0.5 * rms(0.0f)).epsilon(0.04));
  CHECK(rms(-1.0f) == doctest::Approx(2.0 * rms(0.0f)).epsilon(0.04));
}

TEST_CASE("fm: key_rate shortens envelopes up high (less total energy)") {
  Config cfg;
  cfg.push_back({"key_rate", arr({1})});       // times halve per octave up
  cfg.push_back({"fixed", arr({300})});
  cfg.push_back({"t1", arr({0})}); cfg.push_back({"t2", arr({0})});
  cfg.push_back({"t3", arr({0.2})});           // decay l2->l3 over 0.2s at voct 0
  cfg.push_back({"l2", arr({1})}); cfg.push_back({"l3", arr({0})});
  auto energy = [&](float voct) {
    FmRig rig(1000.0, cfg);                     // 1 kHz: 0.2s = 200 samples
    rig.ins[2] = voct;
    double e = 0.0;
    for (int i = 0; i < 400; ++i) { rig.tick(1.0f); e += static_cast<double>(rig.outs[1]) * rig.outs[1]; }
    return e;
  };
  CHECK(energy(1.0f) < 0.75 * energy(0.0f));    // faster decay -> less energy
}

TEST_CASE("fm: key scaling latches at gate rise (mid-note voct change is inert)") {
  Config cfg;
  cfg.push_back({"key_level", arr({-2})});
  cfg.push_back({"fixed", arr({300})});
  cfg.push_back({"t1", arr({0})}); cfg.push_back({"t2", arr({0})}); cfg.push_back({"t3", arr({0})});
  cfg.push_back({"l2", arr({1})}); cfg.push_back({"l3", arr({1})});
  FmRig rig(48000.0, cfg);
  rig.ins[2] = 0.0f;                            // latch at scale 1
  double a = 0.0;
  for (int i = 0; i < 4800; ++i) { rig.tick(1.0f); a += static_cast<double>(rig.outs[1]) * rig.outs[1]; }
  rig.ins[2] = 3.0f;                            // change mid-note: no new rising edge
  double b = 0.0;
  for (int i = 0; i < 4800; ++i) { rig.tick(1.0f); b += static_cast<double>(rig.outs[1]) * rig.outs[1]; }
  CHECK(std::sqrt(b / 4800.0) == doctest::Approx(std::sqrt(a / 4800.0)).epsilon(0.02));
}

TEST_CASE("fm: key_break moves the neutral point") {
  Config cfg;
  cfg.push_back({"key_level", arr({-1})});
  cfg.push_back({"key_break", cnum(1.0)});      // neutral at voct 1
  cfg.push_back({"fixed", arr({300})});
  cfg.push_back({"t1", arr({0})}); cfg.push_back({"t2", arr({0})}); cfg.push_back({"t3", arr({0})});
  cfg.push_back({"l2", arr({1})}); cfg.push_back({"l3", arr({1})});
  auto rms = [&](float voct) {
    FmRig rig(48000.0, cfg);
    rig.ins[2] = voct;
    double s2 = 0.0;
    for (int i = 0; i < 24000; ++i) { rig.tick(1.0f); s2 += static_cast<double>(rig.outs[1]) * rig.outs[1]; }
    return std::sqrt(s2 / 24000.0);
  };
  CHECK(rms(1.0f) == doctest::Approx(2.0 * rms(0.0f)).epsilon(0.04));  // Δ: 0 vs -1
}

TEST_CASE("fm: key scaling defaults are a no-op (bit-identical)") {
  Config base;
  Config withzeros;
  withzeros.push_back({"key_level", arr({0,0,0,0,0,0})});
  withzeros.push_back({"key_rate", arr({0,0,0,0,0,0})});
  withzeros.push_back({"key_break", cnum(0.0)});
  FmRig rb(48000.0, base), rz(48000.0, withzeros);
  rb.ins[2] = 1.5f; rz.ins[2] = 1.5f;           // nonzero voct must not matter
  for (int i = 0; i < 4000; ++i) {
    const float g = (i % 500) < 400 ? 1.0f : 0.0f;
    rb.tick(g); rz.tick(g);
    for (int o = 0; o < 7; ++o) CHECK(rz.outs[o] == rb.outs[o]);
  }
}
```

- [ ] **Step 2: Run to verify they fail**

Run: `cmake --build build -j && ./build/mstests -sf="*test_fm*"` — expect the scaling cases to fail (configs inert), the no-op case to pass.

- [ ] **Step 3: Implement**

Add members (with the other config arrays / runtime state):

```cpp
  double key_break_ = 0.0;
  std::array<double, kOps> key_level_{}, key_rate_{};
  std::array<double, kOps> level_scale_{}, time_scale_{};  // latched per note (migrated)
```

In `init()`, after reading `waves`:

```cpp
    key_break_ = cfg_num(cfg, "key_break", 0.0);
    fill(cfg, "key_level", key_level_, 0.0, -8.0, 8.0);
    fill(cfg, "key_rate", key_rate_, 0.0, -8.0, 8.0);
    level_scale_.fill(1.0);
    time_scale_.fill(1.0);
```

In `process()`, extend the rising-edge block (where `sync_` phase reset lives) to latch scaling:

```cpp
    if (rising) {
      if (sync_) phase_.fill(0.0);
      const double d =
          static_cast<double>(clampn(ins[2], -24.f, 24.f)) - key_break_;
      for (int k = 0; k < kOps; ++k) {
        level_scale_[static_cast<size_t>(k)] =
            clampd(std::exp2(key_level_[static_cast<size_t>(k)] * d), 0.0, 16.0);
        time_scale_[static_cast<size_t>(k)] =
            clampd(std::exp2(-key_rate_[static_cast<size_t>(k)] * d), 1e-3, 1e3);
      }
    }
```

(Delete the old standalone `if (rising && sync_) phase_.fill(0.0);` line it replaces.)

Apply level scaling — multiply it into the existing `lvl` computation:

```cpp
      const double lvl =
          static_cast<double>(clampn(
              static_cast<float>(levels_[static_cast<size_t>(k)]) + ins[4 + k],
              0.0f, 1.0f)) *
          (1.0 - vel_sens_[static_cast<size_t>(k)] +
           vel_sens_[static_cast<size_t>(k)] * static_cast<double>(vel)) *
          level_scale_[static_cast<size_t>(k)];
```

Apply rate scaling — thread `time_scale` into `env_tick`. Change the `env_tick` signature and its four `env_segment` seconds arguments to multiply by `tscale`:

```cpp
void env_tick(OpEnv& e, const EnvCfg& c, int curve, double rate, double tscale) {
  using S = OpEnv::Stage;
  switch (e.stage) {
    case S::S1:
      if (env_segment(e, c.l[0], c.t[0] * tscale, curve, rate)) e.stage = S::S2;
      break;
    case S::S2:
      if (env_segment(e, c.l[1], c.t[1] * tscale, curve, rate)) e.stage = S::S3;
      break;
    case S::S3:
      if (env_segment(e, c.l[2], c.t[2] * tscale, curve, rate)) e.stage = S::Hold;
      break;
    case S::Hold: e.level = c.l[2]; break;
    case S::Rel:
      if (env_segment(e, 0.0, c.t[3] * tscale, curve, rate)) { e.stage = S::Idle; e.level = 0.0; }
      break;
    case S::Idle: e.level = 0.0; break;
  }
}
```

Update the call in `process()`:

```cpp
      env_tick(envs_[static_cast<size_t>(k)], env_cfg_[static_cast<size_t>(k)],
               curve_, rate_, time_scale_[static_cast<size_t>(k)]);
```

Extend `copy_state_from` to migrate the latched scaling:

```cpp
    level_scale_ = o.level_scale_;
    time_scale_ = o.time_scale_;
```

- [ ] **Step 4: Run the tests to verify they pass**

Run: `cmake --build build -j && ./build/mstests -sf="*test_fm*"` — expect all `fm:` cases PASS. Adjust numeric thresholds against real output if needed; never weaken the no-op/latch assertions.

- [ ] **Step 5: Commit**

```bash
git add src/dsp/modules/fm.cpp tests/test_fm.cpp
git commit -m "feat: fm keyboard scaling (level + envelope rate, latched at gate)"
```

---

### Task 4: Docs, attribution, demo patches, full gates

**Files:**
- Modify: `docs/modules.md` (`## fm`: `waves`, `key_break`/`key_level`/`key_rate`)
- Modify: `docs/attribution.md` (expand the `fm.cpp` clause)
- Create: `patches/fm-tests/16-waveforms.patch`, `patches/fm-tests/17-keyscale.patch`
- Test: none new (characterized by Tasks 1–3)

- [ ] **Step 1: Docs**

In `docs/modules.md` under `## fm`, extend the Configs paragraph with:

```markdown
`waves` (per-op operator waveform, bareword array: `sine` default, `half`
half-wave rectified, `abs` full-wave rectified/octave-up, `quarter` bright
windowed pulse, `fold` triangle-wavefolded; an unknown name falls back to
`sine`. All are DC-centered; rich waves alias when pushed, by design).
Keyboard scaling, latched from `v_oct` at the gate's rising edge: `key_break`
(scalar v_oct where scaling is neutral, default 0), `key_level` (per-op level
octave-exponent, `level ×= 2^(key_level·(v_oct−break))`), `key_rate` (per-op
envelope-time octave-exponent, `times ×= 2^(−key_rate·(v_oct−break))`). All
default 0 (no scaling).
```

In `docs/attribution.md`, expand the `fm.cpp` row's adaptation clause to add:

```markdown
Operator waveforms implement the rectified/windowed/folded-sine operator concept (TX81Z/DX11/FS1R family) locally from the mathematics; keyboard level/rate scaling is a standard technique in an original octave-exponent form. No Yamaha tables/code, no GPL/emulator code, and no trademarks are used.
```

- [ ] **Step 2: Demo patches**

Create `patches/fm-tests/16-waveforms.patch`:

```text
# waveforms — a dual EP where the modulators use rectified/folded operator
# waves (abs/fold) for extra bite the sine core can't reach.
clk = clock { bpm: 240 }
wander = random { rate: 2.7, smooth: 0, seed: 21 }
notes = sh { mode: sample }
wander -> notes.in
clk -> notes.trig
key = quant { scale: [0, 3, 5, 7, 10] }
notes -> key.in
poly = alloc { n: 4 }
clk -> poly.gate
key -> poly.voct
ep = voice { notes: poly }
ep.fm = fm { algo: dual,
             waves: [sine, abs, fold, sine, abs, fold],
             ratios: [1, 1, 7, 1, 3.5, 1],
             levels: [.5, .28, .1, .45, .1, .14],
             t1: [.001, .001, .001, .001, .001, .001],
             t2: [.5, .08, .06, .45, .07, .09],
             t3: [1.2, .3, .2, 1.0, .25, .3],
             l2: [.55, .25, .2, .5, .2, .25],
             l3: [.2, 0, 0, .16, 0, 0], curve: log }
poly.voct -> ep.fm.v_oct
poly.gate -> ep.fm.gate
ep.fm -> out.in : 0.35
-0.35 -> out.level
```

Create `patches/fm-tests/17-keyscale.patch`:

```text
# keyscale — a crown bell whose modulators thin out and decay faster up the
# keyboard (key_level/key_rate), so high notes ring short/pure and low notes
# stay fat/long. Wide random voct spread makes the effect obvious.
clk = clock { bpm: 150 }
wander = random { rate: 1.6, smooth: 0, seed: 41 }
notes = sh { mode: sample }
wander -> notes.in
clk -> notes.trig
key = quant { scale: [0, 7, 12, 19] }
notes -> key.in
poly = alloc { n: 4 }
clk -> poly.gate
key -> poly.voct
ep = voice { notes: poly }
ep.fm = fm { algo: crown, freq: 220hz,
             ratios: [1, 1.41, 2.83, 3.14, 4.24, 5.65],
             levels: [.5, .18, .14, .11, .09, .07],
             key_level: [0, -1.2, -1.2, -1.4, -1.4, -1.6],
             key_rate:  [.6, .8, .8, 1.0, 1.0, 1.2],
             t1: [.001, .001, .001, .001, .001, .001],
             t2: [.02, .02, .02, .02, .02, .02],
             t3: [3.0, 2.4, 2.0, 1.7, 1.4, 1.1],
             l2: [.9, .9, .9, .9, .9, .9], l3: [0, 0, 0, 0, 0, 0],
             t4: [1.5, 1.2, 1.0, .8, .6, .5], curve: log }
poly.voct -> ep.fm.v_oct
poly.gate -> ep.fm.gate
ep.fm -> out.in : 0.4
-0.3 -> out.level
```

Verify: `./build/ratsnest check patches/fm-tests/16-waveforms.patch && ./build/ratsnest check patches/fm-tests/17-keyscale.patch` clean; render each with `--stats` and confirm audible + peak under 0 dBFS.

- [ ] **Step 3: Full gates**

Run: `mise run test && mise run test-san && mise run ci` — all green (including `test_docs.cpp`; the `fm-tests/` patches are in a subdirectory so they add no ctest fixtures).

- [ ] **Step 4: Commit**

```bash
git add docs/modules.md docs/attribution.md patches/fm-tests/16-waveforms.patch patches/fm-tests/17-keyscale.patch
git commit -m "docs: fm waveforms + keyboard scaling reference and demo patches"
```

---

## Done criteria

- Bareword arrays lower to `ConfigValue.strs`; numeric arrays unchanged; every existing patch compiles identically.
- `waves` selects five DC-centered operator waveforms per op; unknown → sine; default bit-identical to v1.
- `key_break`/`key_level`/`key_rate` scale per-op level and envelope rate against latched `v_oct`; defaults bit-identical to v1.
- `mise run test`, `mise run test-san`, `mise run ci` all green; docs, attribution, and two demo patches in the diff.
