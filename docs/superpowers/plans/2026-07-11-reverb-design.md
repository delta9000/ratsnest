# reverb — design

Phase 2's `reverb`: the Dattorro 1997 plate ("Effect Design Part 1", JAES
45(9)), per-sample float at the graph rate, hand-written from the paper.
Research basis (2026-07-11): algorithm survey (Dattorro paper read directly;
Mutable Instruments `clouds/dsp/fx/reverb.h`; Signalsmith FDN; Freeverb/Barr
context) + repo-constraint audit. Decisions confirmed by Ben: full Dattorro
plate; freeze gate with ~10 ms smoothing; paper-subtle modulation default.

## Why Dattorro (and what Mutable taught us)

MI's beloved reverb IS a compact Dattorro — Gillet's own comment says
"Griesinger topology" — shrunk for a Cortex-M4 (12-bit samples, 16 K buffer,
32 kHz, one output tap per side, no predelay). At float/48 kHz the compaction
saves ~30 lines and costs the 14-tap stereo field. The full plate fits the
ethos exactly: every delay length, coefficient, and output tap is published
(zero tuning risk), one ~250-line file, no deps, plate coloration is the
ambient sound, and the spec row already says "Dattorro/FDN". The FDN
(lossless freeze, smoother tail, 2–3× code + a delay-tuning search) remains a
clean *second* reverb candidate later. The one MI idea adopted: `mod_depth`
config can push the tank-tap chorusing from the paper's ±13 samples toward
Clouds' ±100 wash.

## Ports

| Port | Kind | Default | Meaning |
|---|---|---|---|
| `in_l`, `in_r` | inputs | 0 | stereo in (summed ×0.5 into the mono tank, per the paper) |
| `decay` | input | 0.5 | tank loop gain, clamped 0..0.9995 (paper §1.3.3: never 1.0 except via freeze) |
| `damp` | input | 0.25 | tank high-frequency damping 0..1 (paper's one-pole in each branch) |
| `mix` | input | 0.3 | equal-power dry/wet, like xfade |
| `freeze` | input | 0 | level-sensed gate (like clock.run): while >0.5, decay→1, damping→0, input muted — the tank holds. Smoothed over ~10 ms |
| `out_l`, `out_r` | outputs | — | 7 decorrelated tank taps per side (paper Table 2), 0.6 gain |
| config `predelay` | seconds | 0.02 | 0..0.25 s, fixed at build |
| config `bandwidth` | 0..1 | 0.9995 | input one-pole lowpass (paper default) |
| config `mod_rate` | Hz | 1.0 | tank allpass tap LFO (quadrature pair), 0..20 |
| config `mod_depth` | samples | 13 | tap excursion 0..100; 13 = paper-transparent, ~80+ = MI wash |

## Structure (constants are the paper's, scaled by rate/29761 in init)

mono sum → predelay → bandwidth lowpass → 4 series input diffusers
(142/107/379/277 @29.8k; coeffs 0.75, 0.75, 0.625, 0.625) → figure-8 tank:

- Branch A: modulated allpass 672 (coeff 0.70) → delay 4453 → damping
  one-pole → ×decay → allpass 1800 (coeff = clamp(decay+0.15, 0.25, 0.5),
  paper Table 1) → delay 3720 → ×decay → into branch B's input.
- Branch B: same shape with 908 / 4217 / 2656 / 3163, cross-feeding A.
- The two allpass tap LFOs run in quadrature at `mod_rate`; linear-interp
  reads (paper: the tiny time-varying lowpass this adds is inaudible at
  small excursions — the modulation is what kills the metallic mode fence).
- Output: yL/yR = 0.6 × (±7 taps each) from the tank lines (paper Table 2
  offsets, scaled), giving the wide decorrelated image from a mono tank.

## Storage & migration

One arena `std::vector<float>` allocated in `init()` (13 lines: predelay +
4 diffusers + 8 tank lines; ≈190 KB at 48 kHz — 10× under the delay module's
accepted 2 MB migration budget), each line a {offset, size, pos} record with
explicit wrap (prime-ish lengths don't fit pow2 masks). `copy_state_from`
= one bounded `std::copy_n` of min(arena sizes) + line positions + 3 filter
states + LFO phase + freeze smoother — no alloc, RT-safe per the delay
precedent. Same-rate migration guaranteed by the controller; min-size copy is
defense.

## Numerics

- Denormal self-flush on the three one-pole states (render now has FTZ, but
  doctest runs `process()` bare — the flush keeps tests deterministic).
  Tail-silence tests assert `< 1e-6`, not exact zero: the arena itself decays
  multiplicatively and is swept by FTZ in real use.
- Freeze blend `f` slews with τ = 10 ms; effective decay = decay + (1−decay)·f,
  effective damp = damp·(1−f), tank input gain = 1−f. Dry path unaffected.
- All config numerics clamped (predelay 0..0.25, bandwidth 0..1, mod_rate
  0..20, mod_depth 0..100); decay/damp/mix inputs clamped per sample.

## Non-goals

Shimmer/pitch feedback, ducking, multiband decay — patchable or later. A
lossless-freeze FDN is the acknowledged future sibling if frozen textures
become central. Cite Dattorro in a header comment (courtesy; the paper asks
only not to misrepresent the algorithm).
