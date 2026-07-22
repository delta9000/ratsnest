# Four-Sample Nonlinear Clean Mode Implementation Plan

> **For Claude:** REQUIRED SUB-SKILL: Use superpowers:executing-plans to implement this plan task-by-task.

**Goal:** Implement opt-in clean processing for `drive` and `fold` with an exact four-host-sample impulse peak, leaving raw output bit-identical.

**Architecture:** A shared six-biquad elliptic IIR performs interpolation and reconstruction around the existing memoryless curve. Both modules use 8x; fold adds first-order ADAA. Coefficients are offline Wolfram designs expressed as normalized second-order sections, while runtime code owns only fixed arrays and scalar state.

**Tech Stack:** C++20, doctest, CMake/CTest, Wolfram Language for offline coefficient provenance, fixed-size direct-form-II-transposed biquads.

---

### Task 1: Check in the coefficient design and shared oversampler

**Files:**
- Create: `tools/design_nonlinear_filters.wls`
- Create: `src/dsp/nonlinear_oversample.h`
- Create: `tests/test_nonlinear_oversample.cpp`

**Step 1: Write failing contract tests**

Test the 8x fixed-rate round trip with an identity callback. Require
silence, finite output, DC settling within `2e-5`, an impulse peak at zero-based
host index 4, tail recovery, and deterministic reset. Exercise the actual
callback API that modules will call.

**Step 2: Verify red**

Run `cmake --build build && build/mstests --test-case="nonlinear oversample:*" --no-skip`.
Expected: compile failure because `nonlinear_oversample.h` does not exist.

**Step 3: Implement the minimal fixed-state core**

Add `Biquad`, `EllipticCascade<Rate>`, and `NonlinearOversampler<Rate>` for
`Rate` 8 or 16. Each host call inserts one input followed by `Rate-1` zeros,
multiplies the interpolator input by `Rate`, invokes the callback at every
high-rate sample, reconstructs, and returns the first decimation phase.
Use six checked-in SOS rows `{b0,b1,b2,a1,a2}` and DF2T state. Flush state
smaller than `1e-20f`; perform no allocation, locking, or variable-work search.

**Step 4: Record reproducible Wolfram generation**

The `.wls` file designs a twelfth-order elliptic lowpass with 0.001 dB ripple,
80 dB attenuation, and normalized edges 0.12/0.145. It converts pairs to DC-normalized
SOS rows and verifies the round-trip impulse peak is four.

**Step 5: Verify green and commit**

Run the focused test and `ctest --test-dir build -R '^unit$' --output-on-failure`.
Commit as `feat: add four-sample nonlinear oversampler`.

### Task 2: Implement clean drive

**Files:**
- Modify: `src/dsp/modules/drive.cpp`
- Modify: `tests/test_drive.cpp`
- Modify: `tests/test_migrate.cpp`

**Step 1: Write failing behavior tests**

Using one persistent module per render, require clean silence, settled constant
transfer, impulse peak 4 in the linear region, finite hostile-input recovery,
and at least 30 dB improvement for the preregistered 2/8 kHz soft/hard cases.
Keep the existing exact raw vectors unchanged.

**Step 2: Verify red**

Run `cmake --build build && build/mstests --test-case="drive clean*" --no-skip`.
Expected: clean still executes raw and fails latency/spectrum assertions.

**Step 3: Implement the clean branch**

Own `NonlinearOversampler<8>`. Preserve the raw statements byte-for-byte and
return before the clean branch. Sanitize only clean inputs, calculate gain at
host rate, and call the existing `std::tanh` or clamp curve inside the fixed
8x callback.

**Step 4: Add state migration**

Copy clean filter state only when old/new modules are both clean and their
drive mode matches. Add migration continuity and quality-change recovery tests.

**Step 5: Verify and commit**

Run drive, migration, oversampler, and full unit tests. Commit as
`feat: add clean drive quality mode`.

### Task 3: Implement clean fold

**Files:**
- Modify: `src/dsp/modules/fold.cpp`
- Modify: `tests/test_fold.cpp`
- Modify: `tests/test_migrate.cpp`

**Step 1: Write failing behavior tests**

Require settled periodic transfer, impulse peak 4 in a linear segment,
hostile-input recovery, and spectral improvement of at least 30 dB for the
preregistered fold-4 cases plus the 48 kHz/10 kHz/fold-8 extreme case.

**Step 2: Verify red**

Run `cmake --build build && build/mstests --test-case="fold clean*" --no-skip`.

**Step 3: Implement the clean branch**

Own `NonlinearOversampler<8>`. Preserve raw arithmetic and ordering exactly.
For clean only, sanitize and overflow-check the host controls, then evaluate
the periodic fold through its analytic first-order ADAA shaper in the 8x
callback. Decimation phase one retains the four-sample peak.

**Step 4: Add matching-quality migration**

Copy clean state only across matching clean instances. Test continuity and
safe reset across quality changes.

**Step 5: Verify and commit**

Run fold, migration, oversampler, and full unit tests. Commit as
`feat: add clean fold quality mode`.

### Task 4: Realtime, safety, and documentation closure

**Files:**
- Modify: `tests/test_nonlinear_oversample.cpp`
- Modify: `docs/modules.md`
- Modify: `docs/attribution.md`
- Modify: `docs/research/2026-07-21-nonlinear-antialiasing.md`

**Step 1: Add measured performance coverage**

Benchmark raw, 8x drive, and 8x+ADAA fold in release mode over a fixed sample
count. Report ns/sample and clean/raw ratios. The acceptance gate is no heap
allocation in `process()`, fixed work, and comfortable headroom for a typical
patch of up to 16 clean nonlinear modules at 48 kHz.

**Step 2: Run safety verification**

Run the focused spectral/latency tests, full CTest, ASan/UBSan build, and
`git diff --check`. Confirm both float round trips still peak at sample four.

**Step 3: Document the user contract**

Document `quality: raw|clean`, raw zero latency/legacy character, clean four
samples (0.083 ms at 48 kHz), 8x drive, 8x+ADAA fold, CPU tradeoff, and manual
parallel-path alignment. Record Wolfram-generated coefficient provenance and
the diagnostic/full-matrix distinction honestly.

**Step 4: Commit**

Commit as `docs: explain nonlinear clean quality modes`.
