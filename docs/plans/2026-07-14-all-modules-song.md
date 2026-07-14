# Minor Machinery Implementation Plan

> **For Claude:** REQUIRED SUB-SKILL: Use superpowers:executing-plans to implement this plan task-by-task.

**Goal:** Add a self-running 4:32 A-minor-pentatonic song patch that uses all 43 registered module types and develops its A motif into a derived B section over VI–V–iv–V.

**Architecture:** A tracker-style bank of 34 musical four-bar steps plus one silent guard step controls a deterministic synthesis graph at 120 BPM. Fixed harmonic and motif sequencers provide the recognizable song material, while generative and live/file modules occupy controlled parallel branches; an eight-channel mix feeds tempo effects and a complete stereo mastering chain.

**Tech Stack:** modsynth text DSL, existing C++20 runtime registry, `mise` build/check/render tasks, shell-based registry coverage check.

---

### Task 1: Establish a failing song fixture

**Files:**
- Create: `patches/minor-machinery.patch`

**Step 1: Write the minimal failing fixture**

Create `patches/minor-machinery.patch` with the title comment and one intentionally unknown declaration:

```text
# minor-machinery -- 120 BPM, A minor pentatonic, 4:32
song = pending_all_modules_song
```

**Step 2: Run validation to verify it fails**

Run: `mise run check -- patches/minor-machinery.patch`

Expected: non-zero exit with an unknown-module diagnostic for `pending_all_modules_song`.

**Step 3: Replace the fixture with the timing and arrangement skeleton**

Add the 480 BPM sixteenth clock, divider tree through a four-bar pulse, and section lanes with 34 musical steps plus a silent anti-wrap guard for lead, bass, pad, percussion, texture, effects, B-selection, and master level. Add `func` slew stages for level lanes.

**Step 4: Run validation**

Run: `mise run check -- patches/minor-machinery.patch`

Expected: `ok`.

### Task 2: Add harmony, A material, and derived B material

**Files:**
- Modify: `patches/minor-machinery.patch`

**Step 1: Add the shared progression**

Add bar-clocked root/third/fifth lanes for F major, E major, D minor, E major. Encode pitches as v/oct offsets around A.

**Step 2: Add the fixed A and B motifs**

Add an eight-step A sequence constrained by `quant { scale: [0,3,5,7,10] }`. Add a derived B sequence that rotates/reverses A's contour and lifts selected notes one octave. Select A or B with `switch` under the arrangement's B lane; capture note changes with `sh`.

**Step 3: Add repeatable ornamentation**

Clock `turing`, `random`, `bern`, and `euclid` from the same tree. Quantize ornament pitch to the same A minor pentatonic set and keep mutation/density arrangement-controlled.

**Step 4: Run validation**

Run: `mise run check -- patches/minor-machinery.patch`

Expected: `ok`.

### Task 3: Build the instrument and optional-input voices

**Files:**
- Modify: `patches/minor-machinery.patch`

**Step 1: Add rhythm voices**

Build kick, clap, and hat paths from `vco`/`noise`, `func`/`adsr`, `svf`, `vca`, and `drive`. Use `logic`, `euclid`, and `bern` for triggers.

**Step 2: Add pitched voices**

Build ladder-filtered bass, folded/LPG lead, and chord pad paths. Use `alloc` with a voice group for overlapping motif plucks.

**Step 3: Add routing and modulation utilities**

Use `math` for bipolar/derived control, `xfade` for A/B timbral morphing, `tempo` for delay time, and `follow` for audio-derived ducking.

**Step 4: Add optional live/file sources**

Instantiate `midi`, `extin`, and `sampler` in low-level parallel branches. Missing live input or optional WAV must leave the offline composition unchanged.

**Step 5: Run validation**

Run: `mise run check -- patches/minor-machinery.patch`

Expected: `ok`.

### Task 4: Add texture, effects, mastering, and sinks

**Files:**
- Modify: `patches/minor-machinery.patch`

**Step 1: Build the stereo instrument mix**

Route up to eight musical buses through `mix`, with section lanes and the kick follower controlling levels.

**Step 2: Add time and granular effects**

Use `delay`, `reverb`, and `grains` as parallel returns. Drive freeze, position, reverse, and density from arrangement and seeded modulation.

**Step 3: Add the mastering chain**

Connect `dcblock` → `eq` → external-sidechain `compressor` → `stereo` → `clipper` → `limiter` → `out`. Attach `scope` and `rec` to the final limited output.

**Step 4: Run validation**

Run: `mise run check -- patches/minor-machinery.patch`

Expected: `ok`.

### Task 5: Verify literal module coverage and audio behavior

**Files:**
- Modify if needed: `patches/minor-machinery.patch`

**Step 1: Compare patch types to the registry**

Extract every `&<name>_info` entry from `src/dsp/registry.cpp`, then verify a declaration of each corresponding type exists in `patches/minor-machinery.patch`.

Expected: no missing types; all 43 registry entries are represented.

**Step 2: Render the opening smoke window**

Run: `build/modsynth render patches/minor-machinery.patch -d 30s -o /tmp/all-modules-smoke.wav --stats 5s`

Expected: successful render, finite non-silent RMS, peak below 1.0, and acceptably low DC.

**Step 3: Render the full arrangement**

Run: `build/modsynth render patches/minor-machinery.patch -d 280s -o /tmp/minor-machinery.wav --stats 8s`

Expected: successful render with audible/non-silent windows across the arranged body, a controlled peak below the limiter ceiling, and a final fade/tail.

**Step 4: Run focused repository tests**

Run: `ctest --test-dir build --output-on-failure -R 'docs|smoke|integration'`

Expected: all selected tests pass.

**Step 5: Commit**

```bash
git add patches/minor-machinery.patch docs/plans/2026-07-14-minor-machinery.md
git commit -m "feat: add all-modules A/B song"
```
