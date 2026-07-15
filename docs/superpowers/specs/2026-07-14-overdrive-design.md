# Overdrive design: `overdrive`

Date: 2026-07-14. Status: approved direction. A single voiced guitar
overdrive/distortion module. Fills the gap the bare `drive` module leaves
(symmetric `tanh`, no tone shaping, no anti-aliasing). Physically-informed-
simplified after Yeh, Abel & Smith, "Simplified, physically-informed models of
distortion and overdrive guitar effects pedals" (DAFx-07,
ccrma.stanford.edu/~dtyeh/papers/yeh07_dafx_distortion.pdf) — the canonical
free source for the Tube Screamer, DS-1 and diode-clipper topologies — with
antiderivative anti-aliasing after Parker, Zavalishin & Le Bivic (DAFx-16) and
Vicanek (vicanek.de/articles/AADistortion.pdf).

## 1. Goal and shape

Almost every classic overdrive/distortion pedal reduces to the same skeleton:
**a pre-emphasis filter → a memoryless waveshaper → a tone filter**, differing
only in gain, clip curve, symmetry, filter corners, and whether the clean
signal is summed back in. One module captures the family; a `model` config
picks the voicing.

`overdrive { model: tubescreamer }` — mono in, mono out.

Not in scope (each is either already composable or a separate module on the
research backlog): phaser/chorus/flanger (need modulated-delay or allpass
modules), wah/tremolo/ring-mod/octave (compose from existing modules — see §8
docs recipes), spring reverb, Fuzz Face (needs a nonlinear circuit solver).

## 2. Interface

| Input | Default | Range (clamped) | Meaning |
|---|---|---|---|
| `in` | 0 | — | audio in (mono), sanitized |
| `drive` | 0.5 | 0..1 | pre-gain into the curve; `gain = 2^(drive * model.drive_span)` |
| `tone` | 0.5 | 0..1 | sweeps the tone-stack corner between model lo/hi |
| `level` | 0.5 | 0..1 | output makeup: `2^((level-0.5) * 4)` (±2 octaves) |
| `mix` | 1.0 | 0..1 | dry/wet crossfade (1 = fully shaped) |

`mix` has a **fixed** default of 1.0, not a per-model one: `InputInfo.def` is a
compile-time constant and the graph seeds each input base from it
(`graph.cpp`), so an input's default cannot vary with config (rule 4 — unpatched
inputs read *their* default). Per-model "clean blend" sweet spots (TS/Klon ≈ 0.5)
are therefore documented recommendations, set in the patch, not code defaults.

- Config `model` (enum): `tubescreamer` (default), `klon`, `rat`, `ds1`,
  `mxr`, `muff`, `clean`. `clean` = the `soft` curve with flat filters, a
  neutral base for from-scratch voicing.
- Output `out` (mono).

`drive`/`tone`/`level` are unipolar knobs; cables sum onto their bases (rule 4).
Config out of range clamps; an unknown `model` falls back to `tubescreamer`
(session never dies, rule 6).

## 3. Signal topology

Per sample (mono), all state fixed-size (rule 3):

```
x0  = sanitize(in)                       # mastering.h sanitize -> 0 on NaN/inf
x   = x0 * drive_gain
xf  = pre_.process(x)                    # model pre-emphasis biquad
# --- clean tap at INPUT level (pre-drive), delay-aligned by decimator (1) ---
clean = align(x0)                        # so mix is a true dry/wet crossfade,
                                         # not a volume control at high drive
# --- shaped path, anti-aliased ---
up[4] = oversampler_.upsample(xf)        # 4x, reuse Oversampler4x
for i in 0..3: sh[i] = shape_adaa(up[i]) # ADAA waveshaper at 4x
w   = decimator_.downsample(sh)          # 4x -> 1x, reuse Decimator4x
w   = dc_.process(w)                     # DC blocker (asymmetry adds DC)
w   = post_shape(w)                      # model post (e.g. RAT slew one-pole)
# --- blend + tone + level ---
y   = mix*w + (1-mix)*clean              # first-class clean blend
yt  = tone_.process(y)                   # model tone biquad, live `tone` corner
out = yt * level_gain
```

The clean⇄shaped crossfade is the first-class "clean blend" that makes a Tube
Screamer/Klon read as themselves (op-amp clip-in-feedback = clean + clipped)
rather than a raw waveshaper to ground. The clean is tapped at input level
(pre-drive), so `mix` is a true dry/wet crossfade rather than a volume control
at high drive — the way a Klon/TS sums roughly-unity clean with the clipped leg.

Coefficients (filters, gains, model constants) recompute at **block rate**
(every 32 samples, Rings/`modal` precedent) driven by `drive`/`tone`; the
filters themselves run per sample. `mix`/`level` interpolate per sample
(`SmoothedValue`, mastering.h) to avoid zipper noise.

## 4. Waveshaper + ADAA (`src/dsp/waveshape.h`, new)

Header-only, `namespace ms::dsp::waveshape`, unit-testable in isolation
(mirrors `resonate.h`). Each curve ships with its antiderivative so first-order
ADAA is exact:

- **`hard`** (silicon hard clip — RAT): `f(x) = clamp(x, -1, 1)`;
  `F(x) = x^2/2` for `|x|<=1` else `|x| - 0.5`.
- **`soft`** (diode/tube soft clip — TS/Klon/MXR/DS-1/Muff): the Vicanek
  shaper `f(x) = x / sqrt(1 + x^2)` (tanh-like, n=2 of Yeh's diode family) with
  antiderivative `F(x) = sqrt(1 + x^2)`. Numerically ideal for ADAA.
- **`cubic`** (optional alt): `f(x) = 1.5x - 0.5x^3` for `|x|<=1` (saturates to
  `±1` beyond); `F(x) = 0.75x^2 - 0.125x^4` for `|x|<=1`, continued linearly.

**Asymmetry** (germanium/diode mismatch, DS-1/TS bias): apply a bias
`g(x) = f(x + b) - f(b)` with `b = model.asym`. The antiderivative composes
(`G(x) = F(x + b) - f(b)*x`), so ADAA stays exact. Asymmetry injects DC — the
`dc_` blocker after decimation removes it.

**First-order ADAA** (Parker/Zavalishin, Vicanek):
```
adaa(x, x1) = |x - x1| < eps ? g((x + x1)/2)          # L'Hopital fallback
                             : (G(x) - G(x1)) / (x - x1)
```
`eps ~ sqrt(FLT_EPSILON)` (~3e-4). Vicanek's branchless form for `soft`:
`(x + x1) / (sqrt(1+x^2) + sqrt(1+x1^2))`. State: previous oversampled input
`x1` per shaper instance (Muff has two).

**Aliasing budget** (Yeh: distortion oversamples 8–10×): ADAA + 4×
oversampling is effectively alias-free even for `hard`, at 4× the shaper math
plus the shared 1-sample decimator latency.

## 5. Voicings (`model` → constants)

Approximate; exact corners/thresholds finalized against Yeh §3 and the
ElectroSmash circuit analyses during implementation (the plan records each, as
the resonator plan did). `drive_span` in octaves of pre-gain across `drive`
0..1.

`rec. mix` is the documented recommended `mix` for that voicing (a patch
setting, not a code default — see §2). Mid-scoop tones are a peaking-EQ cut in
the tone stack.

Drive ranges run hot (drive 0.5 → 2^(span/2)×) so a decaying source keeps
grinding on the sustain, not only the attack transient.

| model | pre-emphasis | curve | asym `b` | drive_span | rec. mix | tone lo→hi |
|---|---|---|---|---|---|---|
| `tubescreamer` | HP ~720 Hz | soft | 0.12 | 10 | 0.5 | 2 k → 6 kHz LP |
| `klon` | HP ~340 Hz | soft (×0.5 thresh, more headroom) | 0.05 | 9 | 0.5 | 3 k → 8 kHz LP |
| `rat` | HP ~90 Hz | hard | 0.05 | 14 | 1.0 | 500 → 6 kHz LP + slew one-pole |
| `ds1` | BP boost (HP 1.1 k) | soft | 0.15 | 12 | 1.0 | mid-scoop (HP 320 / LP 3 k) |
| `mxr` | HP ~160 Hz | soft | 0.12 | 11 | 1.0 | 5 k → 16 kHz LP |
| `muff` | HP ~120 Hz | soft ×2 stages, ~1.2 kHz inter-stage LP | 0.0 | 13 | 1.0 | mid-scoop |
| `clean` | flat (bypass) | soft | 0.0 | 5 | 1.0 | 2 k → 12 kHz LP |

`muff` is the only model with an internal variation: **two** soft-clip stages
with a ~1.2 kHz lowpass between them (guitarscience.net "Re-engineering the Big
Muff π" — the inter-stage band-limiting is what keeps cascaded fuzz smooth).
`rat` adds a one-pole `post_shape` lowpass (~0.3 V/µs LM308 slew rounding).
Both run inside the same oversampled ADAA path.

`tone` maps linearly between the model's `lo`/`hi` corner in log-frequency; the
tone stack is a lowpass (`biquad.h::make_lpf`, added below) except `ds1`/`muff`
which blend a highpass + lowpass for the mid-scoop.

## 6. Shared-code changes

- **`src/dsp/waveshape.h`** — new (see §4).
- **`src/dsp/biquad.h`** — add `make_lpf(freq, sample_rate)` (RBJ lowpass,
  Q=0.707) for the tone stacks; the existing header has hpf/shelf/peaking but
  no lowpass.
- **`src/dsp/modules/overdrive.cpp`** — the module; reuses `Oversampler4x`/
  `Decimator4x` (oversample.h), `Biquad`/`make_*` (biquad.h), `sanitize`/
  `sanitize_clamp`/`db_to_linear`/`flush_denormal`/`SmoothedValue`
  (mastering.h).
- Registry: `overdrive_info` in `modules.h` + `registry.cpp`, like every
  module.

## 7. Runtime integration & robustness

- **Rule 3**: allocation only in `init()`; `process()`/`copy_state_from()`
  never allocate. Fixed state: biquads, oversampler/decimator, DC blocker,
  ADAA `x1`, clean-align buffer, smoothed `mix`/`level`, block counter.
- **Hot reload** (`copy_state_from`): copy all filter/oversampler/DC/ADAA/
  align/smoothed states and the block counter; recompute coefficients on the
  next block. Changing `model` swaps the voicing; states carry (filters
  re-tune on the next block).
- **Denormals**: offline render has no FTZ — `flush_denormal` on biquad and
  DC-blocker states (block-rate flush acceptable, spec precedent).
- **Hostile audio**: feed-forward (no feedback loop), so no sample can
  circulate forever; `sanitize(in)` and param clamps keep every stage finite,
  and the session always recovers (rule 6).
- **Signal conventions** (rule 8): audio ±1; `drive`/`tone`/`level`/`mix`
  unipolar 0..1.

## 8. Docs in the same diff

- `docs/modules.md`: `## overdrive` entry + Tone/dynamics palette row.
- **Guitar-pedal recipes** (completes the "mine the pedals" ask — the free,
  already-composable effects, documented rather than coded):
  - Tremolo: `lfo -> vca.cv`.
  - Ring mod: `math { op: mult }` with an audio-rate carrier.
  - Auto-wah: `follow -> svf.cutoff`, read `svf.bp` (LFO for fixed wah).
  - Octave-up: `math { op: abs } -> dcblock` (note: aliases).
  - Big Muff: `overdrive { model: muff }` (or two `overdrive` stages).
- A demo patch `patches/overdrive.patch`: `string`/`modal` → `overdrive` →
  `reverb`, exercising the module in a guitar-voiced chain.

## 9. Testing (doctest, per file)

- Registry coverage (existing test enforces a docs entry per module).
- `waveshape.h`: each curve matches its definition; each antiderivative matches
  numeric integration; ADAA is continuous through `x == x1` (no NaN, matches
  `g(x)` in the limit); ADAA output stays within the curve's bounds.
- `overdrive`: silence in → silence out (settles to exact 0).
- Drive raises harmonic content: THD/output RMS strictly increases with
  `drive`.
- **Anti-aliasing**: a high sine (e.g. 6 kHz) at high `drive` with `model: rat`
  (hard clip) shows inharmonic (aliased-bin) energy well below a naive
  no-ADAA/no-oversample baseline shaper.
- Models differ: `tubescreamer` (mid hump) vs `ds1` (mid scoop) differ in
  spectral tilt; `rat` (hard) vs `tubescreamer` (soft) differ in odd-harmonic
  structure.
- `mix: 0` routes only the clean (unshaped) path: linear processing, so the
  output adds no distortion harmonics (THD ≈ 0, a pure sine stays a sine);
  `mix: 1` is fully shaped (THD rises with `drive`).
- `tone` sweep moves the output spectral centroid monotonically.
- Hostile inputs (NaN/inf `in`, huge `drive`) recover; hot reload continuity
  (energy within tolerance across the swap); `model` change re-voices without a
  discontinuity blowup.
- Sanitizers: suite passes under `mise run test-san`.
- The demo patch gets its automatic `ratsnest check` ctest; render is
  non-silent and non-clipping.
