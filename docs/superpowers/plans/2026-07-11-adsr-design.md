# adsr — design

Phase 2's `adsr`: the keyboard envelope. Spec §5: "`func` covers AD; sustain
needs its own module." This is `func`'s sibling, deliberately built on the
same skeleton (stage machine, `curve` shapes, double accumulators — the float
stall bug fixed in func applies here identically, so doubles from day one).

## Ports

| Port | Kind | Default | Meaning |
|---|---|---|---|
| `gate` | input | 0 | rises through 0.5 → attack; at or below 0.5 → release |
| `attack` | input | 0.005 | seconds to reach 1.0 |
| `decay` | input | 0.15 | seconds from 1.0 down to sustain |
| `sustain` | input | 0.7 | level held while the gate stays high (0..1, live-tracking) |
| `release` | input | 0.3 | seconds from the current level to 0 |
| `out` | output | — | unipolar 0..1 |
| config `curve` | lin\|exp\|log | lin | same shape function as func (`log` ≈ analog: fast-then-slow falls) |

## Semantics

- **Attack** starts on a rising gate edge (edge-detected via `prev_gate_`),
  from the **current level** — a retrigger mid-release picks up where the
  tail is, no click to zero. Same `start_` capture pattern as func's
  `rise_start_`.
- **Decay** runs when attack completes with the gate still high:
  `level = sustain + (1 − sustain) · (1 − shape(progress))`, reading the live
  `sustain` input each sample so a modulated sustain bends the tail of the
  decay toward wherever it's going.
- **Sustain** holds `clamp(sustain, 0, 1)` while the gate is high —
  live-tracking, so sustain is a playable control mid-note.
- **Release** is level-sensed, not edge-sensed: any sample with gate ≤ 0.5
  while attacking/decaying/sustaining enters Release from the current level.
  A gate blip shorter than the attack releases from partway up, exactly like
  a short key tap.
- **Timing is exact**: attack seconds means the peak lands at
  `attack · rate` samples, same `increment()` contract as func. `progress_`
  and `level_` are doubles (func's ULP-stall lesson).
- **Curve mapping note** (documented, not hidden): with func's shared shape
  vocabulary, `log` (√x on the ramp) gives fast-then-slow falls — the analog
  RC character — and is what patches will usually want; `lin` stays the
  default only for consistency with func.

## Non-goals

Velocity scaling (that's a `vca` after the envelope — rule 5), `eor`/`eoc`
pulse outputs (func has them; a keyboard envelope's consumers key off the
gate itself), per-segment curve configs (one curve, like func).
