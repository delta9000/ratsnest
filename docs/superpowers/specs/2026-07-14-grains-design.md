# grains — design

A granular processor: the Clouds core, made generic and composable. Research
basis: Clouds firmware read at source (`pichenettes/eurorack`, `clouds/dsp/`),
Clouds/Beads manuals, Roads' *Microsound* taxonomy, Bencina's "Implementing
Real-Time Granular Synthesis". Decisions confirmed by Ben: live buffer + file
source in one engine, mono in / stereo out, single-module architecture
(approach B below).

## 1. What Clouds is, and what we take

Clouds is four things in one box: a record/freeze buffer, a grain scheduler, a
polyphonic windowed-resampling grain player, and a blend/post-fx section
(dry/wet, stereo spread, feedback, reverb). The last section already exists in
the palette as `xfade`/`mix`, `stereo`, `delay`, and `reverb` — rebuilding it
inside one module would violate design rule 5. So `grains` is only the part
that cannot be built from cables: **buffer + scheduler + grain engine**, mono
in, stereo out (per-grain pan is where granular stereo comes from). Everything
around it composes:

- **Dry/wet** — `xfade` (mono) or `mix` levels (stereo).
- **Feedback** — patch `g.out_l -> g.in : 0.4` through an `svf.hp` or
  `dcblock`; graph feedback edges cost one sample, exactly like a real cable.
  (Clouds high-passes and soft-limits its internal loop; the same hygiene is
  an `svf` and `drive` here.)
- **Reverb / width** — `reverb`, `stereo`.
- **Per-grain random modulation** (Beads' attenurandomizers) — parameters
  latch at grain onset, so `random`/`sh` into `position`/`pitch` plus a
  clocked `trig` reproduces them compositionally. A built-in `scatter` input
  covers the one case cables can't reach: randomizing internally scheduled
  grains.

Buffer sharing is emergent: processing is deterministic, so two `grains` fed
the same input and the same freeze gate hold identical buffers — several
"grain players on one buffer" (different pitch/size/position each) with no new
mechanism, just RAM.

Rejected alternatives: (A) faithful monolith with blend section — duplicates
three existing modules; (C) `gbuf`/`grain`/`spray` decomposition — a named
buffer reference is a non-signal connection type (against the spirit of
rule 2), and emergent sharing already gives multi-player-per-buffer.

## 2. Ports

| Port | Kind | Default | Meaning |
|---|---|---|---|
| `in` | input | 0 | audio to record, ±1 (ignored in file mode) |
| `freeze` | input | 0 | gate; >0.5 halts the record head, grains keep reading |
| `trig` | input | 0 | rising edge seeds exactly one grain (always works, any density) |
| `position` | input | 0.5 | 0..1 clamp. Live and frozen: 0 = newest audio (just behind the record head), 1 = oldest — freezing never re-maps the knob. File: 0 = file start, 1 = end |
| `scatter` | input | 0 | 0..1 clamp; per-grain uniform random position offset, ±scatter/2 of the addressable range, clamped |
| `size` | input | 0.1 | grain length in seconds, clamp 0.005..2 (span further clamped to ¼ buffer) |
| `pitch` | input | 0 | v/oct transposition by resampling, clamp ±4 oct. Upward shifts alias, as in Clouds — part of the sound |
| `density` | input | 10 | internal seeding rate, grains/second, clamp 0..200; 0 = external `trig` only |
| `jitter` | input | 0.5 | 0..1 clamp; 0 = periodic stream (synchronous), 1 = Poisson cloud (asynchronous). Mean rate stays `density` throughout |
| `shape` | input | 0.5 | window morph, 0..1 clamp: 0 boxcar → 0.5 triangle → 1 Hann (Clouds' TEXTURE curve) |
| `spread` | input | 0.5 | 0..1 clamp; per-grain random pan width. 0 = all grains center |
| `reverse` | input | 0 | gate, latched per grain: >0.5 plays the grain's window backward |
| `out_l`, `out_r` | outputs | — | wet grain sum, equal-power panned |
| `onset` | output | — | one-sample 1.0 pulse on any sample where ≥1 grain seeded |

Configs: `secs` 4 (buffer seconds, clamp 0.25..60), `file` (quoted path;
optional), `seed` 1. With `file:` set the buffer is preloaded from the WAV
(mono mixdown like `sampler`, linearly resampled to engine rate at load,
truncated at 60 s), the record head is disabled, and `in`/`freeze` are inert —
static sample material, `position` scrubs the file. A missing/unreadable file
yields a silent buffer; the session never dies. `seed` drives all stochastic
draws (scheduling, scatter, pan); equal seeds produce identical grain streams,
per the palette-wide repeatability convention.

## 3. Internals

**Buffer.** `std::vector<float>` sized to the next power of two ≥ `secs`·rate,
allocated in `init()` (control thread), mask-indexed like `delay` — masked
reads make interpolation wrap-free without Clouds' mirror-tail trick. Live
mode: circular, record head writes every sample unless frozen; logical length
is the full ring. File mode: logical length = resampled frame count; all reads
clamp to `[0, len)` (grains are placed to fit, so clamping only guards edges).
4 s at 48 kHz → 2^18 samples ≈ 1 MB.

**Freeze.** Hard stop of the record head (reads are unaffected; placement
margins keep grains off the seam). While frozen, the first 256 input samples
are captured to a fixed side buffer (continuous with the frozen content's
edge); on unfreeze the writer crossfades from that tail into live input over
256 samples (~5 ms) — Clouds' click-free resume, verbatim.

**Grain pool.** Fixed array of 64 grains, free-list scan on seed, seeds
silently dropped when the pool is saturated — no allocation, no voice
stealing (Clouds behavior; its pool is also 64). Per-grain state latched at
onset: start index, fractional read phase (a double, as in `sampler` — Clouds'
16.16 fixed point is an MCU economy we don't need), phase increment
`2^pitch`, length in samples, shape, pan gains, reverse flag.
Estimated worst case ~64 Hermite reads + windows per sample ≈ trivial on
desktop.

**Scheduling.** One internal clock: after each internal seed, the next onset
is drawn as `dt = base · ((1 − jitter) + jitter · (−ln u))`, `base =
rate/density` samples, `u` uniform (0,1]; `E[−ln u] = 1` so the mean rate is
exactly `density` for every jitter. jitter 0 is a synchronous grain stream
(Roads), jitter 1 has exponential inter-arrival times — a Poisson cloud.
`dt` clamps to ≥1 sample. `density` ≤ 0 disables the internal clock. A rising
edge on `trig` seeds one grain the same sample, independent of the internal
clock (Clouds' TRIG works in the dead zone; here density 0 *is* the dead
zone, made explicit as Beads did).

**Placement.** Let `span = size·rate·max(2^pitch, 1)` (the buffer distance the
read head covers), clamped to len/4. Live: delay behind the write head =
`position·available + span`, `available = len − 2·span` — the margins
guarantee a grain never crosses the moving record head nor out-runs the oldest
sample (Clouds' `eaten_by_play_head` logic). Frozen uses the same formula —
the write-head margin is merely conservative then, and engaging freeze never
jumps grain placement. File: start = `position·(len − span)`. `scatter` adds
a per-grain uniform offset of
±`scatter`/2 of the addressable range before clamping. Reversed grains read
their span backward from its far end.

**Rendering.** Per sample, per active grain: envelope phase `p` in 0..1 over
the grain; folded triangle `tri = 1 − |2p − 1|`; `shape < 0.5` steepens the
slopes — `g = min(tri · 0.5/(shape + 0.01), 1)` — reaching boxcar-with-ramps
at 0; `shape ≥ 0.5` blends toward a raised-cosine map of `tri` via a
4096-entry LUT — Hann at 1. (Exactly Clouds' `Grain::RenderEnvelope`.) Buffer
read: 4-point cubic Hermite at the fractional phase, the same interpolator as
`delay`. Grain sample × envelope × pan gains sums onto `out_l/out_r`. Pan per
grain: `0.5 + spread·(u − 0.5)`, equal-power sin/cos.

**Normalization.** Output scaled by `min(1 + 2·shape, 2) / √max(N, 1)` (N =
active grains) — Clouds' loudness compensation for grain count and window
energy — smoothed by a ~10 ms one-pole so grain births/deaths don't step the
gain.

**Determinism & RT safety.** xorshift32 seeded from `seed`; no allocation,
locks, or I/O in `process()`; all clamps NaN-proof in the `delay` style
(`!(x >= lo)` catches NaN). Smoothed states flush below 1e−20 (offline render
has no FTZ).

**Migration.** A change of source file or a switch between live and file input starts fresh. For a matching source, the live ring copies its samples (min of sizes); a file source keeps its newly loaded WAV. Both migrate the write
head, freeze/crossfade state, scheduler phase, RNG state, and the whole grain
pool (trivially copyable array). Grains whose spans no longer fit (a `secs`
edit shrank the ring) are killed. Frozen textures and sounding grains ride
through a patch edit. ~1 MB memcpy at default size — half of `delay`'s
audited cost.

## 4. The classic patches

Frozen texture pad (the Clouds move):

```text
g = grains { secs: 4, seed: 7 }
ext -> g.in
hold -> g.freeze            # any gate; latch with logic/sh as needed
slow_lfo -> g.position : 0.3
g.out_l -> verb.in_l : 0.8
g.out_r -> verb.in_r : 0.8
```

Clock-synced grain rhythm: `density: 0`, `e = euclid { ... }`,
`e.trig -> g.trig`, `stepseq.cv -> g.pitch`. Pitch-shifter: unfrozen,
`position: 0`, `density: 25`, `jitter: 0`, `size: 80ms`, pitch to taste —
add `reverb` + feedback for shimmer. Tape-stretch: freeze, then crawl
`position` with `func`/`lfo` saw. File scrub:
`g = grains { file: "drone.wav" }` with CV into `position`.

## 5. Non-goals (deferred, deliberately)

- **µ-law / sample-rate "quality" modes** (Clouds lo-fi, Beads' tape
  characters): a future `quality:` config; the engine is float throughout.
- **WSOLA time-stretch, looping-delay, spectral modes**: different players,
  not this module. The palette's `delay` already covers looping-delay
  territory; spectral is its own future project.
- **Built-in blend section**: dry/wet, feedback, reverb, width — compose
  (see §1).
- **Stereo input**: two instances (rule 5), sharing buffers emergently.
- **Per-grain attenurandomizers** beyond `scatter`: compose via `trig` +
  `sh`/`random`, which latch per grain by construction.
- **Voice stealing / unbounded density**: seeds drop silently at pool
  saturation, like the hardware.

## 6. Testing

Doctest, `tests/test_grains.cpp`, same harness as `test_follow.cpp`:
metadata/defaults; silence in → exact silence out; a single `trig`-seeded
grain reproduces the buffer segment under its window (rendered against a
known ramp/sine buffer); `pitch` +1 doubles read speed (zero-crossing count on
a sine buffer); `jitter: 0` onsets are exactly `rate/density` samples apart
(via `onset`); equal seeds → identical output streams; `freeze` stops buffer
writes (grain output unchanged by post-freeze input); file mode granulates
known WAV content, `in` inert; `spread: 0` pans center (l = r); output stays
bounded at density 200 / size 2 (normalization); `reverse` grain plays a ramp
segment backward; hostile inputs (NaN, ±inf on every port) never poison state;
`copy_state_from` keeps a sounding grain stream intact across migration;
registry/docs coverage via the existing `test_docs` sweep.
