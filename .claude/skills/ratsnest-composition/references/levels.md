# Levels: what number to actually write

Every other staging rule in this library is relative — "kick channel ~0.85",
"premaster ≈ −3 dBFS". That is useless on the first pass unless you know what
comes out of a module in the first place. These are measured: each source
patched straight to `out.in` at unity, 4 s render, `--stats`.

## Sources at their defaults

| Source | peak | RMS | crest |
|---|---|---|---|
| `vco { wave: sine }` | 0.0 | −3.0 | 3.0 |
| `vco { wave: tri }` | 0.0 | −4.8 | 4.8 |
| `vco { wave: saw }` | −0.1 | −4.8 | 4.7 |
| `vco { wave: pulse }` | 0.0 | −0.0 | **0.0** |
| `lfo` at audio rate | 0.0 | −3.0 | 3.0 |
| `noise { color: white }` | −0.0 | −4.8 | 4.8 |
| `noise { color: pink }` | −2.2 | −14.6 | 12.4 |
| `random { rate: 8hz }` | −0.4 | −5.1 | 4.7 |
| `fm {}` bare, gated | −0.0 | −7.4 | 7.4 |
| `lpg` struck, saw in | −0.9 | −14.1 | 13.2 |
| `modal {}` struck | −2.1 | −25.2 | 23.1 |
| `string {}` struck | −7.8 | −31.6 | 23.8 |

**Every oscillator is full-scale.** A `vco` is ±1 — it is not a "line level"
you top up, it is the ceiling. Any voice that reaches `out` at unity is already
at the limit, so the cable depth out of a voice is the *only* thing standing
between you and a clipped mix.

Three that surprise people:

- **`pulse` has zero crest.** A square is its own peak everywhere. It reads
  −0.0 dBFS RMS, so it is ~5 dB "louder" than a saw at the same peak and will
  dominate any mix you level by peak.
- **Pink noise is 10 dB quieter than white** at the same peak. Swapping
  `color:` changes the level, not just the tilt.
- **Struck resonators are 20+ dB down in RMS** with high crest. `modal` at
  −25.2 RMS next to a saw at −4.8 is not "quiet", it is a percussive source
  measured over its silences. Level by peak for these, not RMS.

`random` carries **+0.24 DC** at its default. Block it before it reaches audio.

## What processors do to the level

Saw in at unity (−0.1 peak / −4.8 RMS):

| Chain | peak | RMS | change |
|---|---|---|---|
| `fold { fold: 1 }` | −0.1 | −4.8 | none at fold 1 |
| `drive { amount: 0.5 }` | −0.0 | −0.9 | **+3.9 dB RMS, crest 4.7 → 0.9** |
| `overdrive {}` defaults | 0.0 | −6.8 | −2 dB, and +0.0067 DC |
| `ladder { cutoff: 400, res: 0.9 }` | −6.1 | −13.8 | −9 dB |

`drive { amount: 0.5 }` flattens crest from 4.7 dB to 0.9 dB. That is the
measurement behind "on a full mix that's a limiter, not glue" — it is not
subtle and it is not a warmth control.

## Wet returns

Wet-only effects fed a unity saw come back at roughly source level:

| Return | peak | RMS |
|---|---|---|
| `reverb { mix: 1, decay: 0.8 }` | 0.0 | −3.4 |
| `delay { mix: 1, feedback: 0.35 }` | 0.0 | −3.1 |
| `chorus {}` | −0.9 | −9.0 |

So a send at depth 1 into a `mix: 1` return, mixed at level 1, **doubles your
signal**. Sends want 0.2–0.5 and returns want 0.3–0.6; that is why those
numbers appear all over the library.

## The summing arithmetic

Eight saws, each at cable depth 0.11, identical pitch vs. slightly detuned:

| n | identical (RMS) | detuned (RMS) | peak (both) |
|---|---|---|---|
| 1 | −24.0 | −24.0 | −19.3 |
| 4 | −12.0 | −18.3 | −7.2 |
| 8 | −5.9 | −15.3 | −1.2 |

Read off exactly:

- **Correlated sources sum N×** (+12.0 dB at n=4, +18.1 at n=8 — linear).
- **Decorrelated sources sum √N×** (+5.7 dB at n=4, +8.7 at n=8).
- **Peak sums N× either way.** Detuning buys you RMS headroom, never peak
  headroom.

Practical consequences:

- **Unison stacks need `1/n` staging on peak, not on loudness.** A 7-saw
  supersaw at 1/7 each is ~8 dB quieter in RMS than one saw at unity, which is
  why supersaws always feel like they need more level than they should.
- **Doubling a part at the same pitch is +6 dB, not +3.** If you layered a sub
  under a bass and it got loud rather than fat, that is why.
- **Nothing in this DSL normalizes.** `voice` groups sum straight, `mix`
  channels sum straight, multiple cables into one input sum straight. The
  `1/n` is always your job.

## A staging default that works

Start here and adjust from `--stats`, rather than from zero:

| Stage | Depth |
|---|---|
| voice → its own `vca` | 1.0 (let the envelope do the work) |
| `vca` → mixer channel | 0.5–0.8 |
| unison member (n voices) | `0.8 / n` |
| voice → FX send | 0.2–0.5 |
| wet return → mixer | 0.3–0.6 |
| sub / kick channel | 0.85 (the peak anchor) |
| everything else | under the kick |

Premaster peak ≈ −3 dBFS. If you are more than 6 dB away after one render, the
error is a depth, not the mastering chain.
