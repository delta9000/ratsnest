# Mastering Modules Implementation Plan

> **For Claude:** REQUIRED SUB-SKILL: Use superpowers:executing-plans to implement this plan task-by-task.

**Goal:** Add six reusable stereo mastering/bus processors with documented latency, deterministic behavior, and complete hot-reload migration.

**Architecture:** Build simple linear utilities first, then shared dynamics and biquad infrastructure, then nonlinear oversampling, and finally the lookahead limiter. Each module owns fixed scalar/buffer state, links stereo control paths, sanitizes live inputs, and appends to registry order.

**Tech Stack:** C++20, doctest, ratsnest audio-rate module API, CMake/mise, float DSP

---

### Task 1: Shared test helpers and `dcblock`

**Files:**
- Create: `tests/test_dcblock.cpp`
- Create: `src/dsp/modules/dcblock.cpp`
- Modify: `src/dsp/modules.h`
- Modify: `src/dsp/registry.cpp`
- Modify: `tests/test_migrate.cpp`
- Modify: `docs/modules.md`
- Modify: `docs/superpowers/specs/2026-07-10-ratsnest-design.md`

1. Write failing tests for exact metadata/defaults, constant-DC decay below
   -80 dB, 1 kHz pass-through amplitude, stereo independence, cutoff smoothing,
   hostile finite inputs/rates, silence, and state migration.
2. Run `mise run build && ./build/mstests --test-case="*dcblock*"`; verify
   failure because the module is absent.
3. Implement the one-pole high-pass from the design with two channel states,
   smoothed cutoff, finite clamps, and denormal flushing.
4. Append header/registry entries, spec row, and full module-reference section.
5. Run focused tests, mutate each migration copy line to prove load-bearing,
   run `git diff --check`, then commit `feat: add stereo DC blocker`.

### Task 2: `stereo` utility

**Files:**
- Create: `tests/test_stereo.cpp`
- Create: `src/dsp/modules/stereo.cpp`
- Modify: registry/header/spec/reference and `tests/test_migrate.cpp`

1. Write failing tests: width 1 identity, width 0 mono, width 2 doubles side
   in normalized M/S space, equal-power hard-left/right balance, mono/swap gate
   semantics, exact M/S outputs, correlation +1 identical/-1 inverted/near 0
   quadrature or independent test sequences, finite hostile inputs, migration.
2. Implement normalized M/S encode/decode, smoothed width/balance, level-sensed
   mono/swap, and 300 ms exponential correlation accumulators.
3. Verify focused tests and load-bearing migration; document and commit
   `feat: add stereo image utility`.

### Task 3: `compressor`

**Files:**
- Create: `tests/test_compressor.cpp`
- Create: `src/dsp/modules/compressor.cpp`
- Modify: registry/header/spec/reference and `tests/test_migrate.cpp`

1. Write failing metadata and static-curve tests using steady DC: below
   threshold unity, 4:1 ratio above threshold, exact knee endpoints, makeup,
   dry/wet endpoints, and external sidechain behavior.
2. Add time tests at a low sample rate proving fast attack, monotonic release,
   stereo linking (one hot channel attenuates both equally), no image shift,
   silence, hostile finite inputs, and migration mid-release.
3. Implement Giannoulis feed-forward log-domain curve. Detector is
   `max(abs(side_l), abs(side_r), 1e-12)`; compute target positive reduction dB;
   use attack coefficient while target reduction rises and release while it
   falls; apply shared `10^(-gr/20)` and makeup.
4. Verify against independent expected equations in tests, mutation-check
   migration, document sources/semantics, commit
   `feat: add stereo linked compressor`.

### Task 4: shared biquad and `eq`

**Files:**
- Create: `src/dsp/biquad.h`
- Create: `tests/test_eq.cpp`
- Create: `src/dsp/modules/eq.cpp`
- Modify: registry/header/spec/reference and `tests/test_migrate.cpp`

1. Write failing impulse/frequency-response tests for each RBJ section. Measure
   sine steady-state RMS at band center: zero-gain identity, requested shelf/
   bell gain within .15 dB, HP attenuation/passband, stereo match, parameter
   smoothing, hostile inputs/rates, and migration for every active section.
2. Implement a fixed-size transposed-direct-form-II `Biquad` with coefficient
   and state structs; no allocation. Implement RBJ normalized coefficient
   builders from the W3C note.
3. Implement `eq` as HP -> low shelf -> bell1 -> bell2 -> high shelf per
   channel, sharing coefficients. Smooth raw parameters with 20 ms one-poles;
   use exact bypass coefficients for zero-gain sections.
4. Run frequency sweeps at 44.1/48/96 kHz, full tests and CI; mutation-check
   all section states; document and commit `feat: add stereo mastering EQ`.

### Task 5: oversampling infrastructure and `clipper`

**Files:**
- Create: `src/dsp/oversample.h`
- Create: `tests/test_clipper.cpp`
- Create: `src/dsp/modules/clipper.cpp`
- Modify: registry/header/spec/reference and `tests/test_migrate.cpp`

1. Write failing tests for metadata; below-threshold identity after latency;
   hard ceiling; odd symmetry; soft monotonicity; stereo equality; mix
   endpoints; impulse-measured fixed latency; silence; finite hostile inputs;
   migration of every half-band state.
2. Add a spectral regression: process a near-Nyquist sine into hard clipping,
   compare in-band aliased energy against the current non-oversampled hard
   `drive`, and require a material reduction. Confirm the test fails before
   oversampling exists.
3. Implement a fixed 2x polyphase half-band interpolator/decimator and cascade
   it twice for 4x. Coefficients must be explicit, symmetric, independently
   response-tested, and referenced in comments; do not design them ad hoc in
   `process()`.
4. Apply drive, thresholded hard/normalized-tanh soft nonlinearity at 4x, then
   decimate. Delay the dry branch by measured latency for equal-power mix.
5. Run spectral, focused, sanitizer, and migration mutation tests; document
   alias/latency behavior; commit `feat: add oversampled stereo clipper`.

### Task 6: sample-peak `limiter`

**Files:**
- Create: `tests/test_limiter.cpp`
- Create: `src/dsp/modules/limiter.cpp`
- Modify: registry/header/spec/reference and `tests/test_migrate.cpp`

1. Write failing tests for metadata and config clamping; impulse-measured
   lookahead latency; ceiling guarantee on impulses, alternating polarity, and
   stereo-asymmetric peaks; threshold drive; instantaneous attack; monotonic
   release; stereo linking; gain/GR outputs; silence and hostile values.
2. Add an adversarial deterministic sequence test over at least one million
   samples requiring every finite output sample to remain at or below ceiling
   plus a small float tolerance. Explicitly label this sample-peak, not TP.
3. Implement a fixed stereo circular delay allocated in `init()`. Detector
   observes current undelayed max peak after threshold gain; derive required
   gain against ceiling; attack immediately, release exponentially; apply the
   envelope to the lookahead-delayed pair.
4. Handle zero lookahead without indexing underflow. Migration bounded-copies
   the arena, write index, envelope, and detector history without allocation.
5. Verify all tests and mutation-check migration. Update docs with latency and
   “not true peak” warning; commit `feat: add lookahead stereo limiter`.

### Task 7: integrated demo and final gates

**Files:**
- Create: `patches/mastering.patch`
- Modify: `docs/modules.md`

1. Build a deterministic stress/demo patch using a bright rhythmic source and
   chain `dcblock -> eq -> compressor -> clipper -> limiter -> stereo -> out`.
   Add `scope` probes to compressor and limiter gain reduction.
2. Check and render 60 seconds. Parse float32 WAV; require zero non-finite
   samples, non-silence, sample peak within configured limiter ceiling plus
   tolerance, and stable ten-second RMS windows.
3. Run `mise run test`, `mise run test-san`, `mise run ci`, every patch through
   `ratsnest check`, documentation coverage, and `git diff --check`.
4. Listen when playback is available; report honestly if not. Commit
   `feat: add mastering chain demo patch`.

### Deferred follow-up: standards-based metering

Plan separately: a non-audible `meter` probe plus `ratsnest analyze` implementing
BS.1770-5 K-weighting, channel weights, absolute/relative gates, ITU true-peak
reconstruction, EBU 400 ms/3 s windows, integrated LUFS, loudness range, and
published conformance vectors. Do not couple it to the six-module plan or use
its absence to weaken limiter/clipper tests.
