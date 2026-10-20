# Core Audio Quality Implementation Plan

> **For Claude:** REQUIRED SUB-SKILL: Use superpowers:executing-plans to implement this plan task-by-task.

**Goal:** Replace the ordinary VCO's weak high-frequency waveform generation with shared band-limited tables and preserve float headroom until the live audio-device boundary.

**Architecture:** A process-wide immutable table bank is built on the non-RT initialization path and shared by every VCO. Saw and triangle read the brightest safe table for the current frequency; pulse is the difference of two phase-shifted band-limited saws. Engine and hot-reload mixing remain unclipped, while a small tested helper clamps only the final PipeWire device buffers.

**Tech Stack:** C++20, doctest, CMake/mise, Wolfram Language reference analysis.

---

### Task 1: Add integrated oscillator quality regressions

**Files:**
- Modify: `tests/test_vco.cpp`

**Step 1: Add a reusable integrated-alias measurement**

Add a coherent-frequency helper that computes a one-sided DFT with the existing Goertzel routine, sums energy in expected harmonic bins, and compares it with total AC energy. Use an exact integer number of cycles so leakage is not counted as aliasing.

Add separate tests for saw and triangle at approximately 1, 4, and 8 kHz. Require saw integrated alias-to-harmonic energy below `-60 dB` and triangle below `-70 dB`. Keep the existing single-bin regression until the new implementation is green, then retain both as readable coverage.

Add a pulse test at widths `0.25`, `0.5`, and `0.75` proving finite output, mean close to `2*pw-1`, both polarities, and bounded peak.

**Step 2: Run the focused tests and verify RED**

Run:

```bash
cmake --build build
./build/mstests --test-case="*integrated alias*"
```

Expected: saw and triangle quality tests fail against the existing polyBLEP/naive implementation for high pitches.

**Step 3: Commit the failing tests**

```bash
git add tests/test_vco.cpp
git commit -m "test: raise oscillator aliasing quality bar"
```

### Task 2: Implement the shared band-limited oscillator bank

**Files:**
- Create: `src/dsp/bandlimited_osc.h`
- Create: `src/dsp/bandlimited_osc.cpp`
- Modify: `src/dsp/modules/vco.cpp`
- Modify: `docs/attribution.md`
- Modify: `docs/modules.md`

**Step 1: Define the table-bank interface**

Create an immutable `BandlimitedOsc` API with:

```cpp
class BandlimitedOsc {
 public:
  static const BandlimitedOsc& instance();
  float saw(float phase, float frequency, float sample_rate) const;
  float triangle(float phase, float frequency, float sample_rate) const;
};
```

Use 2048-sample tables and linear wraparound interpolation. Store all samples contiguously and provide an integer harmonic-limit-to-level lookup so `process()` performs no allocation, locking, or binary search.

**Step 2: Generate closely spaced harmonic ceilings during initialization**

Generate exact ceilings `1..64`, followed by approximately semitone-spaced ceilings up to 1023 harmonics, the highest non-Nyquist bin representable by a 2048-point table. For every level, construct:

```text
saw(p) = -(2/pi) * sum(sin(2*pi*h*p)/h, h=1..H)
triangle(p) = (8/pi^2) * sum(cos(2*pi*h*p)/(h*h), odd h<=H)
```

Use double-precision oscillator recurrences while constructing tables, retain one consistent Fourier-series gain across every level, then store floats. Construction occurs through the function-local static returned by `instance()` and is first requested from `Vco::init()`, never from `process()`.

For a current frequency, choose the table with the largest ceiling no greater than `floor(0.5*sample_rate/frequency)`, capped to the bank. Handle zero/non-finite frequency defensively.

**Step 3: Route VCO waveforms through the bank**

Keep analytic sine and existing phase/sync behavior. Replace naive triangle and polyBLEP saw reads with the bank. Construct pulse as:

```cpp
const float shifted = phase - pw - std::floor(phase - pw);
v = bank.saw(shifted, f, rate_) - bank.saw(phase, f, rate_)
    + 2.0f * pw - 1.0f;
```

Do not claim hard sync is alias-free; its arbitrary discontinuity remains a future residual-correction task.

**Step 4: Run focused oscillator tests and verify GREEN**

Run:

```bash
cmake --build build
./build/mstests --test-case="*alias*"
./build/mstests --test-case="*pulse*"
./build/mstests --test-case="*sync*"
./build/mstests --test-case="*zero crossings*"
```

Expected: all focused oscillator tests pass.

**Step 5: Record provenance and behavior**

Document that the tables are locally generated from the standard Fourier series for classical waveforms, conceptually aligned with the project's existing Välimäki/Pekonen/Nam oscillator attribution. Update the VCO module documentation to say ordinary free-running waveforms are table-band-limited while hard sync can still alias.

**Step 6: Commit**

```bash
git add src/dsp/bandlimited_osc.h src/dsp/bandlimited_osc.cpp src/dsp/modules/vco.cpp tests/test_vco.cpp docs/attribution.md docs/modules.md
git commit -m "feat: add band-limited oscillator tables"
```

### Task 3: Preserve float headroom and clamp only the audio device

**Files:**
- Create: `src/engine/audio_safety.h`
- Modify: `src/engine/engine.cpp`
- Modify: `src/engine/rt_runner.cpp`
- Modify: `src/engine/pipewire_out.cpp`
- Modify: `tests/test_engine.cpp`
- Modify: `tests/test_runner.cpp`
- Modify: `README.md`

**Step 1: Write failing headroom tests**

Replace `master output hard-clips to +/-1` with a test proving an `out` value of `3.0` is returned unchanged by `Engine::process()`.

Add a runner hot-swap/crossfade test whose finite mix exceeds unity and prove it is not clamped. Add unit coverage for a new device-boundary helper that maps finite values outside `[-1,1]` to the endpoints and maps non-finite values to zero.

**Step 2: Run focused tests and verify RED**

Run:

```bash
cmake --build build
./build/mstests --test-case="*headroom*"
./build/mstests --test-case="*device safety*"
```

Expected: engine and runner headroom tests fail because both currently clamp; the helper test initially fails to compile because it does not exist.

**Step 3: Implement boundary-only clipping**

Create an allocation-free inline helper in `audio_safety.h` that sanitizes and clamps planar stereo buffers. In `Engine::process()`, assign final node outputs directly after existing per-module non-finite sanitization. In `RtRunner`, remove the clamp around hot-reload crossfade sums. In `PipewireOut::Impl::on_process`, call the helper immediately after the render callback and before publishing buffer chunks.

Do not clip offline float-WAV rendering.

**Step 4: Run focused tests and verify GREEN**

Run:

```bash
cmake --build build
./build/mstests --test-case="*headroom*"
./build/mstests --test-case="*device safety*"
./build/mstests --test-case="*crossfade*"
```

Expected: all focused output tests pass.

**Step 5: Document output semantics and commit**

State in `README.md` that float renders preserve headroom above 0 dBFS, live playback is safety-clamped at the device boundary, and intentional mastering should use `clipper`/`limiter` in the patch.

```bash
git add src/engine/audio_safety.h src/engine/engine.cpp src/engine/rt_runner.cpp src/engine/pipewire_out.cpp tests/test_engine.cpp tests/test_runner.cpp README.md
git commit -m "feat: preserve float output headroom"
```

### Task 4: Full quality and safety verification

**Files:**
- Modify if measurements require truthful adjustment: `tests/test_vco.cpp`
- Modify if behavior wording needs correction: `docs/plans/2026-07-21-core-audio-quality-design.md`

**Step 1: Run the normal suite**

```bash
mise run test
```

Expected: all CTest targets pass.

**Step 2: Run warnings-as-errors and sanitizers**

```bash
mise run ci
```

Expected: warnings-as-errors build, normal tests, and ASan/UBSan tests pass.

**Step 3: Measure representative render cost**

Render a representative patch before and after with `/usr/bin/time`, recording realtime factor and peak memory. The shared bank must not be duplicated per VCO, and the patch must remain comfortably realtime.

**Step 4: Re-run the Wolfram spectral sweep**

Import or reproduce the implemented table selection and confirm the C++ thresholds are conservative across the audible sweep. If measured results differ, fix the implementation rather than weakening tests without a documented reason.

**Step 5: Review the diff and commit any verification-only corrections**

```bash
git diff --check
git status --short
```

Commit only scoped corrections with an appropriate conventional prefix.
