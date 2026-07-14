# `follow` — envelope follower (audio→CV bridge)

**Status:** design approved 2026-07-13. Phase 4 "sound design" module. The
audio→CV bridge: it measures the amplitude envelope of an audio signal and
emits it as a control voltage, plus a threshold gate.

## Purpose

Turn an audio signal into modulation. The motivating use is *audio-derived
sidechain*: point `follow` at a kick bus or a full mix and use its `env` to
duck a mixer level or open a filter, without needing an explicit trigger
envelope. The `gate` output additionally converts sustained audio into a
clocking/gating signal (audio-to-gate).

`follow` is the only Phase 4 member that is pure DSP — no file or PipeWire
I/O — so it obeys design rule 3 exactly like every existing module and needs no
new engine machinery.

## Design decisions

Resolved during brainstorming; each rejected alternative is deferred, not
forbidden.

1. **Mono input.** A single `in` port, per the compose-with-cables ethos
   (rule 5). Follow a stereo source by summing to mono first or following one
   channel. `env`/`gate` are inherently mono anyway. *(Rejected: stereo-linked
   `in_l`/`in_r` like the mastering modules.)*
2. **Peak and RMS, config-selected.** `mode: peak | rms`, default `peak`.
   Peak rectifies (`|in|`) for transient-reactive detection; RMS squares
   (`in²`) for energy/loudness detection. The attack/release ballistics double
   as the RMS averaging window, so both modes share one code path.
3. **`thresh` input with fixed hysteresis.** The gate compares the *smoothed*
   `env` against a `thresh` input; it turns on at `thresh` and off at
   `0.75 · thresh`. The fixed relative hysteresis prevents chatter when the
   envelope hovers near the threshold. *(Rejected: a separate `hyst` knob; a
   fixed 0.5 threshold with no param.)*
4. **Cable gain, not a sensitivity knob.** `env` is the detected linear
   amplitude. Scale weak sources at the patch cord (`quiet -> f.in : 4`), the
   same way the rest of the palette scales signals (rules 4/5). *(Rejected: a
   `sens` multiplier input — redundant with cable depth.)*
5. **Linear domain, honest range.** Detection and ballistics run in the linear
   domain (a CV output, not a gain computer — contrast the compressor's dB/log
   domain). `env` is `≥ 0` and **not** clamped on top: for input within ±1 it
   lands in `0..1`, and it rises past 1 if driven harder. An envelope follower
   that clamped away hot signals would lie about them.

## Ports

```text
follow { mode: peak }
  inputs:  in 0, attack 0.01, release 0.1, thresh 0.1
  config:  mode = peak | rms      (default peak)
  outputs: env, gate
```

| Port | Range / default | Meaning |
|---|---|---|
| `in` (in) | audio ±1 | signal to follow (mono) |
| `attack` (in) | `0.0001..10` s, def `0.01` | rise time constant |
| `release` (in) | `0.0001..10` s, def `0.1` | fall time constant |
| `thresh` (in) | `0..8`, def `0.1` | gate on-threshold (unipolar), scaled like a `vca` gain |
| `env` (out) | `≥ 0`, unipolar | detected linear amplitude; `0..1` for input within ±1, unclamped on top |
| `gate` (out) | `0` / `1` | high above 0.5, per the gate convention |

`mode` is a compile-time config (cannot be modulated), matching the palette's
waveform/mode configs.

## DSP (per sample)

1. **Sanitize** `in` (NaN/Inf → 0), as the mastering modules do.
2. **Rectify by mode:** `x = |in|` for `peak`, `x = in²` for `rms`.
3. **One-pole ballistics** with asymmetric coefficients (the compressor's
   formula):
   `alpha_a = 1 − exp(−1/(attack · rate))`, `alpha_r` likewise from `release`.
   `state += (x > state ? alpha_a : alpha_r) · (x − state)`, then
   `flush_denormal(state)`.
4. **Envelope:** `env = sqrt(state)` in `rms` mode, else `env = state`. (RMS of
   a full-scale sine ≈ `0.707`; peak of the same ≈ `1.0`.)
5. **Gate with hysteresis** on the smoothed `env`: latch high when
   `env > thresh`, low when `env < 0.75 · thresh`, else hold the previous
   latch. Output `1.0` / `0.0`.

Parameter clamps: `attack`, `release` clamp to `0.0001..10 s`; `thresh` clamps
to `0..8` (`≥ 0`). All via the existing `sanitize_clamp` helper.

## State & hot reload

`copy_state_from` migrates the smoothed `state` and the boolean gate latch, so
the envelope and gate survive a hot reload with no discontinuity — matching
every other stateful module. Runtime state keys on (name, type) per the engine
convention.

## Testing (doctest; runs under `test` and sanitized `test-san`)

New `tests/test_follow.cpp`:

- **DC convergence:** const `in = 0.5`, `peak` mode → `env → 0.5`; `rms` mode
  → `env → 0.5`.
- **RMS vs peak of a full-scale sine:** `rms` → ≈ `0.707`, `peak` → ≈ `1.0`.
- **Ballistics timing:** a step input reaches ~63% of target in one `attack`
  period; decays likewise over one `release` period (tolerance-checked).
- **Silence decays to ~0;** exact-zero input stays exactly `0`.
- **Gate hysteresis:** a slow ramp across `thresh` shows the gate rising at
  `thresh` and falling at `0.75 · thresh`, with no chatter while hovering
  between the two.
- **NaN/Inf input** yields finite output (sanitize).
- **`copy_state_from`** migrates both the envelope state and the gate latch.

The existing `tests/test_docs.cpp` registry-coverage test requires a
`## \`follow\`` section in `docs/modules.md`; adding that entry is part of the
work.

## Files

- `src/dsp/modules/follow.cpp` (new) — implementation + `follow_info`
- `src/dsp/modules.h` — `extern const ModuleInfo follow_info;`
- `src/dsp/registry.cpp` — add `&follow_info` to `kRegistry`
- `tests/test_follow.cpp` (new) — globbed in automatically
- `docs/modules.md` — new `## \`follow\`` entry (registry-coverage requirement)

## Non-goals (YAGNI)

Deferred, each reachable later without redesign: stereo input, a `sens`
knob, a `hyst` knob, and a dB-scaled `env` output.
