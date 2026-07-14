# Resonator design: `modal` and `string`

Date: 2026-07-14. Status: approved direction (per-physics modules, in + strike
exciter); details finalized by research against the Rings source
(github.com/pichenettes/eurorack, MIT). This pulls the "physical-modeling voice
(Rings-alike)" item out of the main spec's Phase 5+ out-of-scope list — update
that list in the same commit that lands the first module.

## 1. Goal and shape

A Rings-like resonator capability, decomposed the modsynth way (rule 5: small
single-purpose modules; grains precedent: MI core, periphery on cables). Two
modules that share one parameter language:

- **`modal`** — a parallel bank of up to 64 two-pole bandpass (Simper SVF)
  resonators on a stretched-harmonic series. Bells, bars, drums, plates.
- **`string`** — an extended Karplus-Strong loop: Hermite-read delay line,
  3-tap FIR + SVF-lowpass loop damping, optional dispersion allpass, bridge
  nonlinearity, noise-FM. Plucked/struck strings, gongs at the extremes.

Rings' third model (sympathetic strings) is a **patch recipe**, not DSP: in the
hardware it is just N strings where string 0's output feeds the others at
`0.2/n` gain and structure picks interval ratios. All of that is cables and
constants here (see §7).

Explicitly not ported (each is either implicit behavior banned by rule 4 or
periphery that already exists as modules): strum auto-detection and the onset
detector (gates are explicit), normalization probing (no normalling),
input noise gate, internal limiter and reverb (`drive`/`limiter`/`reverb`),
note CV median filter (software CVs are clean), polyphony (voice groups
instance the whole module per voice — no 64-mode budget split needed).

## 2. Shared parameter language

Both modules expose the same five macro parameters with the same meaning as
Rings, all modulatable inputs (cables sum onto bases):

| Input | Default | Range (clamped) | Meaning |
|---|---|---|---|
| `freq` | 220 Hz | see per-module | fundamental (base for `v_oct`) |
| `v_oct` | 0 | — | pitch offset, +1 doubles |
| `structure` | 0.5 | 0..0.9995 | inharmonicity: modal = stiffness curve; string = dispersion |
| `brightness` | 0.5 | 0..1 | spectral tilt of excitation and decay |
| `damping` | 0.5 | 0..1 | decay time (exponential mapping) |
| `position` | 0.25 | 0..1 | strike/pickup position comb |
| `in` | 0 | — | external excitation audio, always summed |
| `strike` | 0 | edge | fires the internal pitch-aware exciter |

Default `position` is 0.25, not 0.5: at 0.5 the cosine comb zeroes every odd
mode including the fundamental (struck-at-center hollowness) — a bad default.

Effective fundamental `f0 = clamp(freq, lo, hi) * 2^v_oct` (clamped again after
scaling), normalized to `f = f0 / rate` for all internal math. Frequency-domain
constants below assume normalized f.

Both modules recompute expensive coefficients at **block rate** (every 32
samples of the per-sample `process()`, and on the first sample after init),
Rings-style; `position` and the string's FIR coefficients/delay are
interpolated per sample to avoid zipper noise. NaN-hostile inputs must not
poison persistent state (delay-module precedent: `!(x >= lo)` clamps).

## 3. `modal`

`modal { modes: 64 }` — inputs as §2; outputs `out`, `odd`, `even`.

- Config `modes`: 2..128, rounded to even (Rings requires even resolution),
  default 64. Mode filters whose frequency would exceed `0.49` normalized are
  dropped (high notes run fewer filters).
- **Topology**: parallel `stmlib`-style Simper trapezoidal SVFs, bandpass
  outputs summed; input pre-attenuated by 0.125 (Rings' scaling).
- **Mode placement** (per block): `stiffness = stiffness_curve(structure)`;
  starting at `harmonic = f`, `stretch = 1`, each mode i sits at
  `harmonic * stretch`, then `stretch += stiffness`,
  `stiffness *= (stiffness < 0 ? 0.93 : 0.98)`, `harmonic += f`.
- **Stiffness curve** (piecewise, from Rings' LUT generator, evaluated
  directly — no LUT needed on desktop):
  - `g < 0.25`: `-(0.25 - g) * 0.25`
  - `0.25 <= g < 0.3`: `0` (harmonic plateau)
  - `0.3 <= g < 0.9`: `0.01 * 10^(((g-0.3)/0.6) * 2.005) - 0.01`
  - `g >= 0.9`: `1.5 - cos(((g-0.9)/0.1)^2 * pi) / 2`
- **Q / decay**: base `q = 500 * 10^(4 * damping)`; per-mode SVF resonance is
  `1 + mode_freq * q` (constant decay time across modes). Per-mode rolloff
  `q *= q_loss` with `q_loss = brightness*(2-brightness)*0.85 + 0.15`,
  and per mode `q_loss += structure*(2-structure)*0.1 * (1 - q_loss)`.
  Brightness is squared at the top (`b = knob^2`) then attenuated
  `b *= 1 - 0.2*(1-structure)^8` (anti-clip guard), both as in Rings.
- **Position**: per sample, a 2-multiply cosine-series recurrence yields
  `w_n = 0.5 + 0.5*cos(2*pi*n*position)`; mode n's bandpass output is weighted
  by `w_n`. Odd-indexed modes sum to `odd`, even-indexed to `even`,
  `out = odd + even`.
- **Excitation path**: `(in + strike_impulse)` through a lowpass SVF whose
  cutoff follows brightness: `cutoff_knob = b*(2-b)`,
  `fc = f * 2^((cutoff_knob - 0.5) * 8)` (Rings' ±48-semitone span), Q = 1.5.
  `strike` rising edge injects a single-sample impulse of amplitude
  `0.25 * 2^(cutoff_knob^2 * 2) / fc` (energy-normalized against the filter).

## 4. `string`

`string { seed: 1 }` — inputs as §2; outputs `out`, `aux`.

- **Loop**: delay line 8192 samples (min f0 ≈ 5.9 Hz at 48 kHz — replaces
  Rings' sub-11.7 Hz linear upsampler, which is dropped), Hermite fractional
  read → optional dispersion stage → `+ excitation` → 3-tap FIR damping →
  SVF-lowpass damping (accurate tan, Q = 0.5) → write back. Loop delay
  `1/f` minus the SVF phase delay (`2*atan(1/2^(cutoff_semitones/12))/(2*pi)`
  of a period) minus 1 sample for the FIR, clamped to `[4, 8188]`.
- **FIR damping**: `h0 = (1+b)/2, h1 = (1-b)/4`, output scaled by the
  round-trip gain `g_loop`; `b = brightness^2`, interpolated per sample.
- **Decay mapping**: `lf = damping*(2-damping)`;
  `rt60 = 0.07 * 2^(8*lf)` seconds (0.07 s → ~18 s);
  `g_loop = 2^(-120 * delay_samples / (rt60 * rate) / 12)` (floored at
  2^(-127/12)). `damping >= 0.95` crossfades `g_loop -> 1`, `b -> 1`,
  SVF cutoff → 0.4999 (lossless drone), factor `20*(damping-0.95)`.
- **IIR damping cutoff**: `f * 2^(min(24 + 48*damping^2 + 24*brightness^2, 84)
  / 12)`, clamped at 0.499.
- **Structure → dispersion** (bipolar with dead zone):
  `s < 0.24: d = (s-0.24)*4.166` (curved-bridge, gong); `0.24..0.26: d = 0`
  (pure KS); `s > 0.26: d = (s-0.26)*1.35135` (stiff string). Then:
  - `d > 0`: fraction `d*(2-d)*0.475` of the loop delay moves into a
    first-order allpass with gain `-0.618*d/(0.15+|d|)` (piano dispersion),
    only when both segments stay ≥ 4 samples.
  - `d > 0.75`: filtered-noise delay FM, amount `(4*(d-0.75))^2 * 0.025`,
    noise lowpassed at `2^((brightness-1)*4)` (rattle/hiss).
  - `d < 0`: bridge nonlinearity — dead-zoned asymmetric rectifier
    (`±0.025` dead zone, negative side ×1.5) FMs the delay by
    `d^2 * 0.01`, with DC-blocked loop crossfaded in by `-d`.
- **Position**: folded `p = 0.5 - 0.98*|position - 0.5|`. `aux` taps the delay
  line at `delay * p` (second pickup); `out` is the loop filter output minus
  that tap — the pluck-position comb Rings applies at its poly mix. Patches
  recover the raw loop as `out + aux`.
- **Excitation path**: `(in + pluck_burst)` through a lowpass SVF at
  `fc = 0.4 * 2^((b*(2-b) - 1) * 9)`, Q = 0.8, then a DC blocker
  (pole `1 - 2*pi*10/rate`), summed into the loop after the delay read.
- **Plucker** (`strike` rising edge, seeded PRNG — config `seed`, equal seeds
  repeat): white-noise burst of `(1/f) * (position*0.9 + 0.05)` samples fed
  through a feedback comb (`gain = (1-position)*0.8`, period = burst length,
  halved until < 256) and a lowpass SVF at `min(8*fc_exciter, 0.499)`, Q = 1.

## 5. C++ composable core

The two modules share `src/dsp/resonate.h` (header-only, `ms::dsp` internals),
the "composable abstraction" at the C++ level:

- `SimperSvf` — trapezoidal SVF with `set_f_q_fast` (5th-order tan
  approximation, valid 16 Hz..16 kHz) and `set_f_q_accurate` (`std::tan`),
  templated/branchless per-sample tick returning lp/bp/hp. The existing
  `svf.cpp` module keeps its own inline implementation (different resonance
  law); extracting it is out of scope.
- `CosineSeries` — 2-multiply recurrence generating `cos(2*pi*n*x)`.
- `DelayLine` — power-of-two ring with Hermite fractional read and
  first-order allpass segment (the string uses one instance for the main loop
  and one for the dispersion allpass).
- `DampingFir` — the 3-tap symmetric lowpass with per-sample coefficient
  interpolation.
- `stiffness_curve(g)`, `semitones_to_ratio(x) = 2^(x/12)`, dead-zoned
  dispersion mapping, `Plucker`.

Everything obeys rule 3: allocation only in `init()` (delay/mode arrays sized
there), `process()` and `copy_state_from()` never allocate.

## 6. Runtime integration

- Registry: `modal_info`, `string_info` in `src/dsp/modules/modal.cpp` /
  `string.cpp`, wired into `registry.cpp` and `modules.h` like every module.
- Hot reload: `copy_state_from` copies SVF states, delay lines (bounded
  `min(size)` copies), cosine/plucker/PRNG state, previous-strike level, and
  block-rate counters, delay-module style. Changing `modes:` migrates the
  overlapping prefix.
- Denormals: offline render has no FTZ — flush decaying states (mode SVF
  integrators, loop filters, DC blockers) below 1e-20 as in `delay.cpp`.
- Errors: config out of range clamps (session never dies); `modes` rounds to
  the nearest even value in range.

## 7. Sympathetic-strings recipe (docs/modules.md)

```text
main = string { seed: 2 }
s1 = string { seed: 3 }
s2 = string { seed: 4 }
seq -> main.v_oct
seq -> s1.v_oct
0.58496 -> s1.v_oct          # +7.02 st: just fifth, 12*log2(3/2)/12
seq -> s2.v_oct
1 -> s2.v_oct                # +1 oct
gate -> main.strike
main.out -> s1.in : 0.1      # Rings couples at 0.2/n of (out - aux)
main.out -> s2.in : 0.1
0.9 -> s1.damping            # sympathetics ring longer, per part.cc
0.9 -> s2.damping
main.out -> mix.in1
s1.out -> mix.in2 : 0.6
s2.out -> mix.in3 : 0.6
```

Detune shimmer: duplicate a string with `0.001 -> dup.v_oct`. Chord ladders
(octave/fifth stacks) are just constants; `quant` supplies scales.

## 8. Testing (doctest, per module file)

- Registry coverage: docs/modules.md entries (the existing test enforces this).
- Silence in, no strike → exact silence out (both modules).
- Strike → nonzero output that decays; higher `damping` → strictly longer
  time to fall below a threshold (RT60 ordering, both modules).
- `string` at `damping: 1` sustains (RMS at t+5 s within 1 dB of t+1 s).
- Pitch: strike `string` at 220 Hz, autocorrelation peak at `rate/220` ± 1
  sample; `+1 v_oct` halves the lag. `modal` bp peak near f0 (goertzel).
- Position comb: `position: 0.5` vs `0.25` differ; `modal` fundamental weight
  at 0.5 ≈ 0 (odd/even outputs verify the split).
- Structure: `modal` structure 0.27 (plateau) yields harmonic mode 2 at 2*f0;
  structure 0.95 stretches it measurably. `string` dead zone: structure 0.25
  equals plain KS output bit-for-bit with dispersion path disabled.
- Determinism: equal seeds → identical strike renders; different seeds differ.
- Hostile inputs: NaN/huge `freq`/`v_oct`/`position` for a block, then sane
  values → output recovers (no stuck NaN state).
- Hot reload: `copy_state_from` mid-ring continues without discontinuity
  (energy within tolerance across the swap); `modes` change migrates prefix.
- Sanitizers: suite passes under `mise run test-san`.

## 9. Docs in the same diff

- `docs/modules.md`: `modal` and `string` entries + sympathetic recipe.
- Main design spec: move "physical-modeling voice (Rings-alike)" out of the
  Phase 5+ out-of-scope list, pointing here.
- A demo patch `patches/rings.patch` exercising both modules.
