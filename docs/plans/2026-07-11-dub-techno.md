# Generative Dub Techno Patch Implementation Plan

> **For Claude:** REQUIRED SUB-SKILL: Use superpowers:executing-plans to implement this plan task-by-task.

**Goal:** Add a stable, self-running Basic Channel-inspired dub-techno patch with sparse generative harmony and slowly evolving echoes.

**Architecture:** A 122 BPM clock tree triggers kick, hats, and two-bar chord stabs. Seeded random voltage sampled at chord boundaries selects pitches from a narrow D-minor/Dorian set; detuned chord oscillators pass through an enveloped driven ladder, dark modulated delay, and stereo plate, with slow LFO evolution and kick ducking.

**Tech Stack:** modsynth patch DSL, existing C++20 DSP modules, `modsynth check`, offline WAV rendering, Python standard-library WAV inspection

---

### Task 1: Create the generative dub-techno patch

**Files:**
- Create: `patches/dub-techno.patch`
- Reference: `docs/plans/2026-07-11-dub-techno-design.md`
- Reference: `patches/aurora.patch`
- Reference: `patches/pluck.patch`

**Step 1: Confirm the target does not exist**

Run: `test ! -e patches/dub-techno.patch`
Expected: exit status 0.

**Step 2: Write the complete patch**

Create `patches/dub-techno.patch` with these named sections and connections:

- Timing: `master = clock { bpm: 244 }` as eighth notes, divided into
  `quarter`, `half`, `bar`, and `two_bar` gates.
- Harmony: seeded `random` into `sh`, triggered by `two_bar`, then `quant`
  using D minor/Dorian degrees `[0, 2, 3, 5, 7, 9, 10]`. Keep its cable
  scale small enough that motion favors nearby chord roots.
- Stab: three saw/triangle oscillators tuned as root, minor third, and fifth;
  send the quantized root to all three and add fixed `v_oct` constants of
  `0.25` and `0.58333` to the upper voices. Mix them into a VCA controlled by
  a short `func` envelope triggered on `two_bar`.
- Tone: a driven `ladder` after the stab VCA. Modulate its cutoff with the
  stab envelope plus two quiet, slow LFOs at incommensurate rates around
  0.02–0.07 Hz; keep resonance below self-oscillation.
- Dub chain: soft `drive`, then two dark delays around dotted-eighth and
  three-eighth timing at 122 BPM. Keep feedback below `0.8`, modulate time
  only subtly with long slew, and feed both returns into a stereo `reverb`
  with long decay and moderate damping.
- Rhythm: quarter-note sine kick with fast pitch drop; quiet white-noise hats
  high-passed by `svf` and triggered on eighths with quarter-note attenuation.
- Mix: dry stab low, echoes and reverb prominent, kick centered, hats slightly
  off-center. Route `kick_env` negatively to wet-channel mixer levels for
  subtle ducking. Set `out.level` conservatively.
- Comments: explain tempo derivation, seeded repeatability, chord voicing,
  delay times, and each long evolution cycle.

**Step 3: Check patch syntax**

Run: `mise run build && ./build/modsynth check patches/dub-techno.patch`
Expected: build succeeds and checker prints `ok`.

**Step 4: Commit the patch**

```bash
git add patches/dub-techno.patch
git commit -m "feat: add generative dub techno patch"
```

### Task 2: Render and tune the patch

**Files:**
- Modify: `patches/dub-techno.patch`
- Create temporarily: `/tmp/dub-techno.wav`

**Step 1: Render one minute**

Run: `./build/modsynth render patches/dub-techno.patch -d 60s -o /tmp/dub-techno.wav`
Expected: exit status 0 and a non-empty float32 WAV.

**Step 2: Inspect numerical safety and loudness**

Run a read-only Python standard-library script that parses the WAV `data`
chunk as little-endian float32 and reports sample count, non-finite count,
peak, RMS, first-half RMS, and second-half RMS.

Expected:

- non-finite count is 0;
- peak is greater than 0.05 and below 1.0;
- RMS is greater than 0.005 and below 0.35;
- neither half is silent.

**Step 3: Inspect evolution windows**

Extend the same analysis to print RMS and peak for six consecutive ten-second
windows.

Expected: all windows are non-silent and at least two window RMS values differ
materially, confirming long-term evolution rather than a static loop.

**Step 4: Listen and tune when playback is available**

Listen for restrained event density, a centered deep kick, dark repeats,
audible movement over 20–40 seconds, and no metallic runaway or obvious master
clipping. Adjust only constants in `patches/dub-techno.patch`, then repeat
Steps 1–3. If playback is unavailable, state that explicitly and rely on the
render checks without claiming subjective listening verification.

**Step 5: Re-check after tuning**

Run: `./build/modsynth check patches/dub-techno.patch`
Expected: `ok`.

Run: `git diff --check`
Expected: no output.

**Step 6: Commit tuning if the patch changed**

```bash
git add patches/dub-techno.patch
git commit -m "fix: tune dub techno patch dynamics"
```

Skip this commit when Task 2 requires no source changes.

### Task 3: Final verification

**Files:**
- Verify: `patches/dub-techno.patch`

**Step 1: Run the full test suite**

Run: `mise run test`
Expected: all tests pass.

**Step 2: Run the warnings-as-errors gate**

Run: `mise run ci`
Expected: build and tests pass with warnings treated as errors.

**Step 3: Confirm repository scope**

Run: `git status --short`
Expected: no new task-related changes. Preserve the pre-existing user edit in
`patches/pluck.patch`; do not stage, modify, or revert it.
