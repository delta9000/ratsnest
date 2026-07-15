# Shadow Lattice Implementation Plan

> **For Claude:** REQUIRED SUB-SKILL: Use superpowers:executing-plans to implement this plan task-by-task.

**Goal:** Build Meridian album track 2 as a deterministic five-minute dark techno patch whose central instrument combines strum polyphony, Turing pitch, and interlocking Euclidean rhythms.

**Architecture:** A 122 BPM clock tree drives a 40-block tracker-style arrangement, three Euclidean rhythm families, and one seeded Turing pitch source. A four-voice `alloc`/`voice` group turns the mono generative note stream into overlapping polyphony; explicit percussion, ambience, effects, and arrangement-controlled gain stages form the rest of the song.

**Tech Stack:** ratsnest patch DSL, existing DSP modules documented in `docs/modules.md`, `mise` validation/test tasks, offline WAV renderer.

---

### Task 1: Establish the arranged clock and control skeleton

**Files:**
- Create: `patches/shadow-lattice.patch`
- Reference: `patches/meridian.patch`
- Reference: `docs/plans/2026-07-11-shadow-lattice-design.md`

**Step 1: Write the song header and clock tree**

Document the album position, D-minor center, 122 BPM tempo, fixed seeds,
five-minute target, and section timeline. Add a sixteenth-note master clock at
488 BPM and divide it successively to eighth, quarter, half, bar, and four-bar
signals:

```text
master   = clock { bpm: 488 }
eighth   = div { by: 2 }
quarter  = div { by: 2 }
half     = div { by: 2 }
bar      = div { by: 2 }
four_bar = div { by: 4 }
master -> eighth.in
eighth -> quarter.in
quarter -> half.in
half -> bar.in
bar -> four_bar.in
```

At 122 BPM, forty four-bar blocks last about 314.8 seconds. Reserve the final
two blocks for fade and silent tail.

**Step 2: Add arrangement lanes**

Create 40-step lanes for kick, percussion density, lattice level, drone level,
Turing mutation, effects send, and master level. Follow the approved timeline;
use held values in `0..1`, with mutation near zero during returns and highest in
the two breakdown blocks. Clock every lane from `four_bar`.

**Step 3: Smooth level controls**

Feed all amplitude and send lanes through `func` slew limiters. Use two-to-five
second fades for parts and an eight-second fall for the master. Do not slew
discrete Euclidean hit counts or rotations.

**Step 4: Add a temporary silent output and validate**

```text
-1 -> out.level
```

Run: `mise run check -- patches/shadow-lattice.patch`

Expected: exit 0 with no parse or graph diagnostics.

**Step 5: Commit**

```bash
git add patches/shadow-lattice.patch
git commit -m "feat: scaffold shadow lattice arrangement"
```

### Task 2: Build the deterministic Turing polyphonic lattice

**Files:**
- Modify: `patches/shadow-lattice.patch`
- Reference: `patches/strum.patch`
- Reference: `docs/modules.md` (`Voice groups`, `alloc`, `turing`, `quant`)

**Step 1: Add shared generative pitch**

Clock a seeded 16-bit Turing register from the eighth-note division. Modulate
its `prob` input from the arrangement mutation lane, keeping the declared base
at zero so locked sections can truly repeat. Scale its output and quantize to a
narrow D-natural-minor pitch set such as `[0, 2, 3, 5, 7, 8, 10]`.

**Step 2: Add the 11-step note rhythm and sampling boundary**

Create `euclid { steps: 11, hits: 3, rot: 0 }`, clocked from eighth notes.
Use arrangement CV to raise density by at most four hits and change rotation
only at section boundaries. Sample the quantized Turing pitch with `sh` on the
Euclidean trigger so pitch never changes inside a sounding note.

**Step 3: Allocate four overlapping notes**

```text
lattice_alloc = alloc { n: 4 }
lattice_euclid.trig -> lattice_alloc.gate
lattice_note -> lattice_alloc.voct

lattice = voice { notes: lattice_alloc }
```

Use the allocator velocity/spread banks only if they materially improve the
patch; do not add routing that the song does not need.

**Step 4: Implement the four-voice group**

Inside the group, build a deterministic oscillator/filter/envelope voice using
an ADSR or LPG with a release long enough for consecutive allocations to
overlap. Broadcast slow timbral modulation from outside the group. Keep the
per-voice summed cable depth at or below `0.16` before effects so four coincident
voices retain headroom. The voice-group mechanism stamps one design four times;
register separation comes from allocator `spread` or a bank-compatible control,
not four manually declared group members.

If validation proves the allocator cannot provide the required register
separation, replace the group with four explicit voices driven by rotated
Euclidean lanes, as permitted by the design; do not modify C++ or DSL behavior.

**Step 5: Validate and render the isolated lattice**

Route the lattice sum temporarily to `out.in`, raise `out.level` to a safe
audible value, and run:

```bash
mise run check -- patches/shadow-lattice.patch
mise run render -- patches/shadow-lattice.patch -d 45s --stats 5s -o /tmp/shadow-lattice-voices.wav
```

Expected: validation succeeds; render stats are finite and non-silent, with no
clipping. Listen for overlapping tails and stable pitch within each strike.

**Step 6: Commit**

```bash
git add patches/shadow-lattice.patch
git commit -m "feat: add shadow lattice polyphonic turing voice"
```

### Task 3: Add the Euclidean rhythm section and drone

**Files:**
- Modify: `patches/shadow-lattice.patch`

**Step 1: Build the grounded drum cycle**

Use a 16-step Euclidean lane for the main pulse, with arrangement-controlled
hits constrained to a club-readable range. Synthesize the kick from a short
pitch-swept sine envelope. Add a dark noise transient for secondary impact,
but keep it subordinate to the kick.

**Step 2: Build the asymmetric metallic cycle**

Use a 13-step Euclidean lane with fixed seed sources and section-controlled
rotation. Excite a short resonant band-pass or LPG voice from filtered noise
and a pitched oscillator component. Pan it away from center and keep its decay
short enough that the 13-step cycle remains legible.

**Step 3: Add restrained high-frequency motion**

Derive a hat or tick layer from the 16-step family with a different rotation.
Use filtered white noise, a very short exponential envelope, and arrangement
density. Avoid a constant straight-sixteenth wash.

**Step 4: Add the four-voice opening drone**

Construct a quiet D-minor harmonic bed from four explicit oscillator/filter
paths or a second safe voice grouping. Its arrangement level is strongest in
the intro, breakdown, and outro. Slow bipolar modulation may move paired filter
cutoffs in opposite directions, but pitch remains stable.

**Step 5: Validate and render representative rhythm**

```bash
mise run check -- patches/shadow-lattice.patch
mise run render -- patches/shadow-lattice.patch -d 110s --stats 10s -o /tmp/shadow-lattice-groove.wav
```

Expected: validation succeeds; the render becomes audibly denser around 1:03,
and statistics remain finite without clipping.

**Step 6: Commit**

```bash
git add patches/shadow-lattice.patch
git commit -m "feat: add shadow lattice euclidean rhythm section"
```

### Task 4: Complete spatial mix, ducking, and master arrangement

**Files:**
- Modify: `patches/shadow-lattice.patch`

**Step 1: Assemble the dry stereo band mix**

Place kick and sub information in the center, offset lattice and metallic
elements, and spread drone pairs left/right. Apply arrangement level after each
sound generator so silent parts cannot leak into effects sends.

**Step 2: Add kick-derived ducking**

Subtract a restrained amount of kick envelope from the lattice, drone, and
effects-return mix levels. Ensure every modulated mixer level has enough base
gain to avoid clamping the part permanently to zero.

**Step 3: Add dark synchronized delay and short reverb**

Use `tempo { bpm: 122 }` to own delay times, with one straight or dotted
division per side. Low-pass the delay through its tone parameter and keep
feedback below runaway values. Send the lattice and metal voice selectively.
Use a short, damped reverb for drone and upper lattice voices; duck both returns.

**Step 4: Connect the arranged master fade**

Sum dry mix and returns into a final stereo mixer, feed `out.left/right`, set a
negative base on `out.level`, and add the slewed master lane. The last blocks
must reach silence early enough for the reverb and delay tail to decay before
the render ends.

**Step 5: Run static validation**

Run: `mise run check -- patches/shadow-lattice.patch`

Expected: exit 0 with no diagnostics.

**Step 6: Commit**

```bash
git add patches/shadow-lattice.patch
git commit -m "feat: complete shadow lattice mix and effects"
```

### Task 5: Verify the complete album track

**Files:**
- Modify if needed: `patches/shadow-lattice.patch`
- Verify: `tests/`

**Step 1: Render the full deterministic take**

```bash
mise run render -- patches/shadow-lattice.patch -d 318s --stats 10s -o /tmp/shadow-lattice-full.wav
```

Expected: finite non-silent statistics during musical sections, no clipping or
NaNs, and silence or a safely decayed tail before 318 seconds.

**Step 2: Render listening windows**

If the renderer has no seek option, render from the start to at least 65, 130,
245, and 290 seconds and inspect the relevant regions in the resulting files.
Confirm intro restraint, first lattice overlap, unstable breakdown mutation,
and the locked final lattice. Adjust only patch constants and arrangement
values; do not add engine scope.

**Step 3: Re-render and prove determinism**

Render the same duration twice and compare hashes:

```bash
mise run render -- patches/shadow-lattice.patch -d 30s -o /tmp/shadow-lattice-a.wav
mise run render -- patches/shadow-lattice.patch -d 30s -o /tmp/shadow-lattice-b.wav
sha256sum /tmp/shadow-lattice-a.wav /tmp/shadow-lattice-b.wav
```

Expected: identical SHA-256 hashes.

**Step 4: Run the project gates**

```bash
mise run check -- patches/shadow-lattice.patch
mise run test
```

Expected: patch validation and all CTest tests pass.

**Step 5: Review the final diff**

Run: `git diff --check HEAD~1 -- patches/shadow-lattice.patch`

Expected: no whitespace errors. Confirm `patches/meridian.patch` and the user's
unrelated `patches/strum.patch` modification are absent from the diff.

**Step 6: Commit final tuning if needed**

```bash
git add patches/shadow-lattice.patch
git commit -m "fix: tune shadow lattice arrangement and headroom"
```

Skip this commit when verification required no changes.
