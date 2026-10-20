# Nonlinear Quality Modes Implementation Plan

> **For Claude:** REQUIRED SUB-SKILL: Use superpowers:executing-plans to implement this plan task-by-task.

**Goal:** Add explicit per-module `raw` and four-sample-latency `clean` quality modes to `drive` and `fold`, preserving legacy output exactly while reducing difficult representative in-band alias error by at least 30 dB.

**Architecture:** Keep the existing formulas as an untouched raw branch selected once from config. This original plan's Tasks 3 onward are superseded by `docs/plans/2026-07-21-four-sample-nonlinear-clean.md`, after measurement rejected the legacy oversampler and the user approved a four-sample budget.

**Tech Stack:** C++20, doctest, existing `Oversampler4x`/`Decimator4x`, existing ADAA `waveshape::Shaper`, Wolfram Language as an offline numerical oracle, CMake/mise.

---

### Task 1: Measure the candidate algorithms with Wolfram

**Files:**
- Create: `docs/research/2026-07-21-nonlinear-antialiasing.md`
- Reference: `src/dsp/oversample.h`
- Reference: `src/dsp/waveshape.h`
- Reference: `src/dsp/modules/drive.cpp`
- Reference: `src/dsp/modules/fold.cpp`

**Step 1: Define the exact legacy curves in Wolfram**

Use `mcp__Wolfram__WolframLanguageContext` for nonlinear audio antialiasing,
then evaluate definitions equivalent to the C++:

```wl
rawSoft[x_, amount_] := Tanh[x (1 + 9 Clip[amount, {0, 1}])]
rawHard[x_, amount_] := Clip[x (1 + 9 Clip[amount, {0, 1}]), {-1, 1}]
rawFold[x_, fold_, sym_] :=
  1 - 4 Abs[Mod[(x Max[fold, 0] + sym + 1)/4, 1] - 1/2]
```

Confirm the periodic fold identity:

```wl
FullSimplify[
  rawFold[x, 1, 0] == (2/Pi) ArcSin[Sin[Pi x/2]],
  Assumptions -> Element[x, Reals]
]
```

Expected: `True` away from equivalent piecewise boundary conventions.

**Step 2: Validate an analytic periodic fold antiderivative**

For `k = Floor[(x + 1)/2]` and `s = (-1)^k`, validate:

```wl
foldAnti[x_] := With[{k = Floor[(x + 1)/2]},
  With[{s = (-1)^k}, s (x - 2 k)^2/2 + If[s < 0, 1, 0]]]
```

Check numerically on a dense grid that `foldAnti'[x]` equals `rawFold[x,1,0]`
away from odd-integer corners, and that the value is continuous at every
corner in `-17..17`.

Expected: maximum derivative error below `10^-10`; left/right continuity
error below `10^-10`.

**Step 3: Compare 1x raw, 1x ADAA, 4x only, and 4x+ADAA**

At 48 and 96 kHz, sweep coherent tones `{250, 500, 1000, 2000, 4000, 8000,
10000}` Hz and representative controls:

```wl
amounts = {0, .25, .5, .75, 1};
folds = {1, 2, 4, 8};
syms = {-.5, 0, .5};
```

Use a 64x generated curve followed by a long linear-phase low-pass and
decimation as the reference. Align candidate latency before calculating:

- wanted-harmonic energy;
- integrated in-band error excluding DC;
- passband gain error;
- peak output;
- clean/raw error improvement in dB.

Expected decision: choose one uniform fixed-work clean topology if it clears
30 dB everywhere practical. Prefer 4x+ADAA if 1x ADAA misses wavefold corners;
do not increase beyond 4x without showing a failing acceptance case.

**Step 4: Record the reproducible result**

Write the Wolfram definitions, sample rates, sweep points, alignment method,
summary tables, chosen topology, measured worst case, and exact thresholds to
`docs/research/2026-07-21-nonlinear-antialiasing.md`. Record the existing
half-band coefficients explicitly and flag their unresolved provenance; do not
claim Wolfram generated them.

**Step 5: Commit**

```bash
git add docs/research/2026-07-21-nonlinear-antialiasing.md
git commit -m "docs: model nonlinear antialiasing quality"
```

### Task 2: Pin the raw compatibility and public config contract

**Files:**
- Modify: `tests/test_drive.cpp`
- Modify: `tests/test_fold.cpp`

**Step 1: Add helpers that accept quality config**

Change the test helpers to accept `const char* quality = nullptr`. Only push a
`{"quality", ConfigValue{quality, 0, {}, false}}` entry when non-null.

**Step 2: Add failing metadata tests**

Require each module to expose a `quality` config whose values are `raw` and
`clean`. Keep `drive`'s existing `mode` config first.

```cpp
CHECK(std::string(info->configs.back().name) == "quality");
CHECK(std::string(info->configs.back().enum_vals[0]) == "raw");
CHECK(std::string(info->configs.back().enum_vals[1]) == "clean");
```

**Step 3: Add raw bit-identity vectors**

For finite representative inputs, compare absent quality and explicit raw with
exact `==`, not `Approx`. Also compare each against the current closed-form
formula using the same float operations and ordering as production.

Cover:

```cpp
// drive
mode in {"soft", "hard"}
input in {-2, -.7, -.1, 0, .1, .7, 2}
amount in {0, .1, .5, 1}

// fold
input in {-6, -1.5, -.25, 0, .25, 1.5, 6}
fold in {0, 1, 2, 4}
sym in {-.5, 0, .5}
```

**Step 4: Run the focused tests and verify red**

Run:

```bash
cmake --build build
build/mstests --test-case="*quality*" --no-skip
```

Expected: metadata assertions fail because neither module declares `quality`.
Raw identity assertions should already pass.

**Step 5: Add only the config selection skeleton**

In both modules add:

```cpp
constexpr const char* kQualities[] = {"raw", "clean", nullptr};
```

Append `{"quality", kQualities}` to config metadata, parse in `init()`, and
store `bool clean_`. Do not change `process()` yet.

**Step 6: Run focused tests and verify green**

Run the same focused command.

Expected: PASS, including exact raw compatibility.

**Step 7: Commit**

```bash
git add tests/test_drive.cpp tests/test_fold.cpp \
  src/dsp/modules/drive.cpp src/dsp/modules/fold.cpp
git commit -m "feat: declare nonlinear quality modes"
```

### Task 3: Extend the ADAA curve core for exact tanh and periodic fold

**Files:**
- Modify: `src/dsp/waveshape.h`
- Modify: `tests/test_waveshape.cpp`

**Step 1: Write failing curve-definition tests**

Extend `Curve` with planned `Tanh` and `Fold` cases in the tests first. Check:

```cpp
CHECK(f_eval(Curve::Tanh, x) == doctest::Approx(std::tanh(x)));
CHECK(f_eval(Curve::Fold, 1.5f) == doctest::Approx(0.5f));
CHECK(f_eval(Curve::Fold, -1.5f) == doctest::Approx(-0.5f));
CHECK(f_eval(Curve::Fold, 3.0f) == doctest::Approx(-1.0f));
CHECK(f_eval(Curve::Fold, x + 4.0f) == doctest::Approx(f_eval(Curve::Fold, x)));
```

Use exact legacy modulo arithmetic for `f_eval(Curve::Fold, x)` so raw and
clean share the same nominal curve definition.

**Step 2: Extend the numeric-integral test**

Add `Curve::Tanh` and `Curve::Fold`, testing multiple positive and negative
endpoints that cross several fold corners. Compare `f_anti(X)-f_anti(0)` with
a dense trapezoidal integral at relative error `1e-3` or better.

**Step 3: Add hostile-value and recovery tests**

Feed a clean `Shaper` NaN, infinity, and then a valid sine sweep. Require every
output finite and the valid sequence to become audible. This test should fail
until `Shaper::process()` sanitizes before touching `x1_`.

**Step 4: Run and verify red**

```bash
cmake --build build
build/mstests --test-case="waveshape:*" --no-skip
```

Expected: compile failure for missing enum values, followed by the hostile
state test failing before hardening.

**Step 5: Implement the exact curves and antiderivatives**

Add `Tanh` and `Fold` to `Curve`.

For tanh use a stable log-cosh antiderivative:

```cpp
const float a = std::abs(x);
return a + std::log1p(std::exp(-2.0f * a)) -
       std::numbers::ln2_v<float>;
```

For fold use the Wolfram-validated piecewise quadratic. Avoid converting an
unbounded float directly to an integer: sanitize and reduce `x` modulo 4
before determining the segment. Preserve continuity across `-1`, `1`, and
equivalent periodic corners.

At the top of `Shaper::process()`, collapse non-finite input to zero before
computing `dx` or updating history. Keep the small-difference midpoint
fallback.

**Step 6: Run and verify green**

Run the focused command and then:

```bash
ctest --test-dir build -R '^unit$' --output-on-failure
```

Expected: all waveshape and unit tests pass.

**Step 7: Commit**

```bash
git add src/dsp/waveshape.h tests/test_waveshape.cpp
git commit -m "feat: add antialiased tanh and fold curves"
```

### Task 4: Add a shared fixed-latency clean shaper

**Files:**
- Create: `src/dsp/clean_shaper.h`
- Create: `tests/test_clean_shaper.cpp`

**Step 1: Write a failing clean-shaper contract test**

Specify a fixed-state class with this surface:

```cpp
class CleanShaper {
 public:
  void reset(waveshape::Curve curve);
  float process(float x);
};
```

Test silence, bounded constants, odd symmetry for zero-bias curves, hostile
input recovery, and an impulse-response peak at host sample index 1.

**Step 2: Add a static-transfer settling test**

For every curve, feed a constant for 128 samples and compare the settled output
to `f_eval(curve, x)` within the Wolfram-measured tolerance. This catches gain
loss disguised as alias suppression.

**Step 3: Run and verify red**

```bash
cmake --build build
build/mstests --test-case="clean shaper:*" --no-skip
```

Expected: compile failure because `clean_shaper.h` does not exist.

**Step 4: Implement the measured topology**

For the expected 4x+ADAA result, store only:

```cpp
waveshape::Shaper shaper_;
Oversampler4x oversampler_;
Decimator4x decimator_;
```

`process()` sanitizes `x`, upsamples to four fixed stack floats, applies the
ADAA shaper four times, and decimates once. No heap allocation or variable
loop bounds. If Task 1 proves a simpler topology meets every target, implement
that measured topology while retaining the same API and one-sample peak
latency.

**Step 5: Run and verify green**

Run the focused test and the unit test.

Expected: PASS.

**Step 6: Commit**

```bash
git add src/dsp/clean_shaper.h tests/test_clean_shaper.cpp
git commit -m "feat: add shared clean nonlinear shaper"
```

### Task 5: Implement clean drive without changing raw drive

**Files:**
- Modify: `src/dsp/modules/drive.cpp`
- Modify: `tests/test_drive.cpp`
- Modify: `tests/test_migrate.cpp`

**Step 1: Write failing clean static-transfer and latency tests**

Create a persistent drive rig rather than reconstructing the module per
sample. For `quality: clean`, feed constants for 128 samples and compare against
the existing soft/hard formulas. Send an impulse in the linear region and
require its response peak at index 1.

**Step 2: Write a failing targeted alias regression**

Render a coherent 16.1 kHz sine at amount 1. The hard curve's third harmonic
lands at 48.3 kHz and aliases near 300 Hz at 48 kHz. Measure 300 Hz with the
existing Goertzel style and require:

```cpp
CHECK(alias_clean < alias_raw * 0.0316228);  // at least 30 dB
```

Also require the clean fundamental remains within the passband-gain tolerance
recorded by Task 1.

**Step 3: Run and verify red**

```bash
cmake --build build
build/mstests --test-case="drive clean*" --no-skip
```

Expected: clean is still the raw formula, so latency and alias assertions fail.

**Step 4: Implement the clean branch**

Add `CleanShaper clean_shaper_;`. In `init()`, reset it to `Curve::Tanh` for
soft mode or `Curve::Hard` for hard mode. In `process()` preserve the raw branch
verbatim:

```cpp
if (!clean_) {
  outs[0] = hard_ ? std::clamp(x, -1.0f, 1.0f) : std::tanh(x);
  return;
}
outs[0] = clean_shaper_.process(x);
```

Sanitize hostile input and controls only on the clean path so raw compatibility
is not changed.

**Step 5: Migrate clean state only across matching quality**

Copy `clean_shaper_` in `copy_state_from()`. Because `copy_state_from()` does
not receive config metadata, store `clean_` in each instance and copy the
quality state only when `clean_ == other.clean_`; otherwise retain the freshly
reset state.

Add a migration-continuity test for clean drive and a quality-change test that
requires finite output rather than identical history.

**Step 6: Run and verify green**

Run drive, migration, and full unit tests.

Expected: PASS.

**Step 7: Commit**

```bash
git add src/dsp/modules/drive.cpp tests/test_drive.cpp tests/test_migrate.cpp
git commit -m "feat: add clean drive quality mode"
```

### Task 6: Implement clean fold without changing raw fold

**Files:**
- Modify: `src/dsp/modules/fold.cpp`
- Modify: `tests/test_fold.cpp`
- Modify: `tests/test_migrate.cpp`

**Step 1: Write failing clean static-transfer and latency tests**

Use a persistent clean fold rig. After settling, compare constant outputs with
the existing formula across multiple fold periods and symmetry offsets. In the
linear region require impulse peak latency 1.

**Step 2: Write failing alias regressions from the Wolfram sweep**

Use the worst representative coherent cases chosen in Task 1, including at
least 4 kHz with fold 4 and non-zero symmetry. Measure integrated error or the
specific isolated alias bins recorded in the research document. Require at
least 30 dB improvement and no more than the measured passband-gain error.

**Step 3: Add modulation and hostile-input tests**

Sweep fold and symmetry at audio rate while processing a high-frequency tone.
Require finite, bounded output and no persistent silence after one hostile
sample. Do not demand zero modulation sidebands: they are wanted signal.

**Step 4: Run and verify red**

```bash
cmake --build build
build/mstests --test-case="fold clean*" --no-skip
```

Expected: latency and spectral assertions fail while clean still uses raw.

**Step 5: Implement the clean branch**

Add a `CleanShaper` configured with `Curve::Fold`. Preserve the existing raw
statements byte for byte in the raw branch. For clean:

```cpp
const float x = sanitize(ins[0]);
const float gain = std::max(sanitize(ins[1]), 0.0f);
const float sym = sanitize(ins[2]);
outs[0] = clean_shaper_.process(x * gain + sym);
```

Use overflow-safe multiplication or a finite check before the shaper so
hostile finite maxima cannot become infinity.

**Step 6: Add matching-quality state migration**

Mirror the drive rule and tests.

**Step 7: Run and verify green**

Run fold, migration, waveshape, clean-shaper, and full unit tests.

Expected: PASS.

**Step 8: Commit**

```bash
git add src/dsp/modules/fold.cpp tests/test_fold.cpp tests/test_migrate.cpp
git commit -m "feat: add clean fold quality mode"
```

### Task 7: Close spectral, sample-rate, and realtime gaps

**Files:**
- Modify: `tests/test_drive.cpp`
- Modify: `tests/test_fold.cpp`
- Modify if tuning proves necessary: `src/dsp/clean_shaper.h`
- Modify if tuning proves necessary: `src/dsp/oversample.h`
- Modify: `docs/research/2026-07-21-nonlinear-antialiasing.md`

**Step 1: Add the 48/96 kHz representative matrix**

Turn the Task 1 representative points into bounded doctest loops. Cover low
frequency cases to detect dulling, high-frequency cases to detect aliases,
both drive curves, and fold/symmetry combinations. Keep test runtime below one
second per module by using coherent short buffers and Goertzel bins selected by
Wolfram.

**Step 2: Run the quality matrix**

```bash
cmake --build build
build/mstests --test-case="*quality spectrum*" --no-skip
```

Expected: PASS. If a case fails, inspect it in Wolfram before changing a
threshold.

**Step 3: Tune only from measured evidence**

If the uniform 4x+ADAA path misses a target, compare these changes in order:

1. numerical correction to the curve antiderivative;
2. safer small-`dx` threshold;
3. regenerated, attributable half-band coefficients;
4. curve-specific topology.

Do not weaken the 30 dB requirement merely because implementation is harder.
If the ideal reference shows the requirement is physically ill-posed at a
particular boundary, document and replace that point with a defensible one.

**Step 4: Benchmark representative patch cost**

Create a temporary patch outside the repository with 32 raw drive/fold pairs,
then the same 32 clean pairs. Render 30 seconds with `/usr/bin/time` and record
wall time plus peak RSS in the research document. Require comfortably faster
than realtime in the normal build and no allocation inside `process()`.

**Step 5: Commit**

```bash
git add tests/test_drive.cpp tests/test_fold.cpp \
  src/dsp/clean_shaper.h src/dsp/oversample.h \
  docs/research/2026-07-21-nonlinear-antialiasing.md
git commit -m "test: enforce nonlinear clean quality bar"
```

Only stage implementation files that actually changed.

### Task 8: Document behavior and provenance

**Files:**
- Modify: `docs/modules.md`
- Modify: `docs/attribution.md`
- Modify: `tests/test_docs.cpp`
- Modify if behavior differs from the approved design: `docs/plans/2026-07-21-nonlinear-quality-modes-design.md`

**Step 1: Update module documentation**

For both modules document:

- `quality: raw|clean`, default raw;
- raw is the legacy zero-latency character path;
- clean preserves the nominal curve with reduced aliasing;
- clean has exactly one host-sample peak latency;
- parallel raw/clean paths are not automatically delay compensated.

**Step 2: Update attribution**

Extend the existing Parker/Zavalishin/Le Bivic and Vicanek entry to cover the
new locally derived tanh and periodic-fold antiderivatives. If oversampling
coefficients were regenerated, replace the unresolved provenance note with the
actual primary design method and parameters; otherwise leave it explicitly
unresolved.

**Step 3: Pin the documentation in tests**

Update `tests/test_docs.cpp` to require the relevant algorithm names and
quality/latency wording without overfitting whole paragraphs.

**Step 4: Run docs and unit tests**

```bash
cmake --build build
ctest --test-dir build -R '^unit$' --output-on-failure
```

Expected: PASS.

**Step 5: Commit**

```bash
git add docs/modules.md docs/attribution.md tests/test_docs.cpp \
  docs/plans/2026-07-21-nonlinear-quality-modes-design.md
git commit -m "docs: explain nonlinear quality modes"
```

Only stage the design file if implementation evidence required a correction.

### Task 9: Full verification and independent review

**Files:**
- No planned source changes

**Step 1: Verify formatting and worktree scope**

```bash
git diff --check
git status --short
git diff feat/core-audio-quality...HEAD --stat
```

Expected: no whitespace errors and no unrelated files.

**Step 2: Run the complete normal suite**

```bash
mise run test
```

Expected: 60/60 CTest targets pass.

**Step 3: Run warnings-as-errors and sanitizers**

```bash
mise run ci
```

Expected: 60/60 CI targets and 60/60 sanitizer targets pass. If LeakSanitizer
reports its known ptrace incompatibility inside the sandbox, rerun the same
sanitizer CTest outside ptrace; do not treat an environment abort as a pass.

**Step 4: Re-run the focused objective gates**

```bash
build/mstests --test-case="drive clean*" --no-skip
build/mstests --test-case="fold clean*" --no-skip
build/mstests --test-case="*quality spectrum*" --no-skip
```

Expected: all pass and report the Wolfram-derived margins.

**Step 5: Request independent code review**

Use `superpowers:requesting-code-review`. Ask the reviewer to inspect raw
bit-compatibility, curve math, alias metrics, fixed latency, RT safety, hostile
input recovery, migration, provenance, and whether tests can pass through mere
attenuation.

**Step 6: Address findings and repeat affected gates**

Use `superpowers:receiving-code-review` before acting on feedback. Fix every
Critical or Important finding, add a regression, and repeat the full gate after
the final change.

**Step 7: Prepare branch handoff**

Use `superpowers:verification-before-completion`, then
`superpowers:finishing-a-development-branch`. Report the exact branch,
worktree, commits, test totals, quality margins, performance delta, and any
deliberately deferred coverage.
