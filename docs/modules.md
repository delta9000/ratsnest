# Implemented Module Reference

This is the authoritative agent-facing reference for every module currently in
the runtime registry. It documents implemented behavior, not roadmap entries.

## Signal conventions

- Every connection carries a float at audio rate. Audio/bipolar CV is normally
  `-1..1`; unipolar CV is `0..1`; pitch is volts/octave.
- Gates are high above `0.5`. “Rising edge” means crossing from `<=0.5` to
  `>0.5`; a trigger is one sample, while a gate has duration.
- Values in `{ ... }` set input bases or compile-time configs. Cables sum onto
  input bases. Configs (waveform, mode, arrays, seed) cannot be modulated.
- No normalling is implicit. Seeded modules are repeatable. Runtime state
  survives hot reload when module name and type remain unchanged.

## DSL essentials

```text
osc = vco { freq: 110hz, wave: saw }
osc -> filter.in
filter.lp -> amp.in : 0.5       # cable gain
0.58333 -> osc.v_oct            # constants also sum onto inputs
seq = stepseq { steps: [0, 0.25, 0.58333], gates: [1, 0, 1] }
```

Suffixes include `hz`, `ms`, and `s`. Omitted output means the first output
(a bare `stepseq` name reads its `cv`, a bare `svf` its `lp`). Validate with
`modsynth check patch.patch`; `modsynth render ... --stats [Ns]` prints
overall peak/RMS/DC plus a per-window level timeline for mix work.

## Quick palette

| Area | Modules |
|---|---|
| Sources | `vco`, `lfo`, `noise`, `random` |
| Tone/dynamics | `vca`, `adsr`, `func`, `lpg`, `svf`, `ladder`, `fold`, `drive` |
| Routing | `math`, `xfade`, `mix`, `logic`, `switch`, `sh`, `quant` |
| Sequencing | `clock`, `div`, `euclid`, `bern`, `turing`, `stepseq`, `tempo` |
| Effects/I/O | `delay`, `reverb`, `scope`, `out` |

## `vca`

`vca { gain: 1, response: lin }` — inputs `in` 0, `cv` 0, `gain` 1;
output `out`. Gain is `clamp(gain + cv, 0, 8)`, squared for `response: exp`.
Typical use: `env -> amp.cv` with base gain zero.

## `out`

Inputs `in` 0 (both channels), `left` 0, `right` 0, `level` 1; engine outputs
`l`, `r`. It sums mono into each side and applies level. Example:
`mix.out_l -> out.left`, `mix.out_r -> out.right`.

## `vco`

`vco { freq: 440hz, wave: sine, pw: 0.5 }` — inputs `freq`, `v_oct`, relative
linear `fm`, `pw`, `pwm`, rising-edge `sync`; output `out`. Waves: `sine`,
`tri`, `saw`, `pulse`. `+1` v/oct doubles pitch; frequency is band-limited and
clamped below Nyquist.

## `lfo`

`lfo { freq: 1hz, wave: sine, phase: 0 }` — inputs `freq`, rising-edge `reset`,
phase offset; bipolar output `out`. Waves: `sine`, `tri`, `saw`, `square`.
Example: `slow -> filter.cutoff : 300`.

## `clock`

`clock { bpm: 120, run: 1 }` — inputs `bpm`, `run`, rising-edge `reset`;
outputs 50%-duty `out` and normalized `run`. Stop forces clock low but holds
phase. Use BPM 240 for eighth-note gates at a musical 120 BPM.

## `div`

`div { by: 2 }` — inputs `in`, rounded positive `by`, rising-edge `reset`;
output `out`. Produces an approximately 50%-duty divided gate. Coincident reset
and input edge counts as step zero.

## `func`

`func { rise: 0.01, fall: 0.1, curve: lin, cycle: off }` — inputs rising-edge
`trig`, slew `in`, rise/fall seconds; outputs unipolar `out`, `eor` gate, and
one-sample `eoc`. Curves: `lin`, `exp`, `log`; cycle: `off`/`on`.

## `svf`

`svf { cutoff: 1000, res: 0 }` — inputs `in`, cutoff Hz, resonance `0..1`;
outputs `lp`, `bp`, `hp`. Cutoff clamps to `5..0.45 * sample_rate`.

## `noise`

`noise { color: white, seed: 1 }` — no inputs; bipolar `out`. Colors: `white`
or `pink`. Equal seeds produce repeatable streams.

## `math`

`math { op: add }` — inputs `a` 0, `b` 1; output `out`. Ops: `add`, `sub`,
`mult`, `min`, `max`, `abs`, `clip`, `wrap`. `abs` ignores b; clip/wrap use
`abs(b)` as the symmetric range. `mult` is ring modulation.

## `xfade`

Inputs `a` 0, `b` 0, `mix` 0.5 (`0..1`); output `out`. Equal-power rather
than linear, so both inputs are -3 dB at midpoint.

## `drive`

`drive { amount: 0.5, mode: soft }` — inputs `in`, amount `0..1`; output
`out`. Modes: tanh `soft` and clipping `hard`.

## `fold`

Inputs `in` 0, non-negative `fold` 1, symmetry offset `sym` 0; output `out`
folded into `-1..1`. Best paired with a triangle oscillator and modulated fold.

## `sh`

`sh { mode: sample }` — inputs `in`, `trig`; output held `out`. `sample`
captures on trig rising edges; `track` follows while trig is high and holds
while low.

## `ladder`

Inputs `in` 0, `cutoff` 1000 Hz, `res` 0 (`0..1`), `drive` 0 (`0..4`);
four-pole low-pass output `out`. Example: `env -> filter.cutoff : 1800`.

## `lpg`

Inputs `in`, continuous `cv`, rising-edge `strike`, `damp` 0.5, `decay` 0.25 s;
output `out`. Strike excites a vactrol-like amplitude/brightness decay; CV can
hold it open.

## `random`

`random { rate: 1hz, smooth: 1, seed: 1 }` — internally clocked bipolar
generator. Outputs cosine-interpolated `out` and raw `stepped`; smooth clamps
to `0..1`.

## `quant`

`quant { scale: [0,2,3,5,7,9,10], root: 0, transpose: 0 }` — inputs `in`
v/oct, root semitones, transpose v/oct; outputs quantized `out` and one-sample
`changed`. Empty scale is chromatic; ties snap down.

## `mix`

Eight inputs `in1..in8` and additive level CVs `lvl1..lvl8`; configs `levels`
(missing = 1) and `pans` (`-1` left, `0` center, `1` right; missing = center).
Outputs `out_l`, `out_r`. Negative level CV is useful for sidechain ducking.

## `scope`

`scope { label: name }` — input `in`, no patch output. Captures last/peak state
for watch and future TUI tooling without altering the signal path.

## `delay`

Inputs `in` 0, `time` 0.4 s (`0..10`), `pitch` 0 v/oct, `feedback` 0.35,
equal-power `mix` 0.5, feedback `tone` 8000 Hz, `slew` 0.1 s; output `out`.
Effective time is `time * 2^-pitch`. The dark, DC-blocked, saturated feedback
can self-oscillate above 1. Example: `{ time: 375ms, feedback: .65, mix: 1 }`.

## `adsr`

Inputs `gate`, `attack` .005 s, `decay` .15 s, `sustain` .7, `release` .3 s;
unipolar `out`. Config curve: `lin`, `exp`, `log`. Retriggers and releases from
the current level rather than jumping.

## `reverb`

Stereo Dattorro plate. Inputs `in_l`, `in_r`, `decay` .5, `damp` .25, `mix`
.3, `freeze` gate; outputs `out_l`, `out_r`. Configs: `predelay` .02 s,
`bandwidth` .9995, `mod_rate` 1 Hz, `mod_depth` 13 samples. Freeze smoothly
mutes input and makes the tank lossless while held.

## `euclid`

Inputs `clock`, rising-edge `reset`, `steps` 16 (`1..64`), `hits` 4
(`0..steps`), wrapped `rot` 0. Outputs one-sample `trig` and clock-width `gate`.
Example: `euclid { steps: 16, hits: 5, rot: 3 }`.

## `bern`

`bern { prob: .5, seed: 1 }` — input `gate`; mutually exclusive gate-width
outputs `a`, `b`. Each rising edge routes to a with probability `prob`, else b.
Use after Euclid for repeatable variations.

## `logic`

`logic { op: and, threshold: .5 }` — inputs `a`, `b`, threshold; normalized
gate `out`. Ops `and`, `or`, `xor`, `not`, `cmp`; Boolean ops threshold inputs,
not negates a, and cmp tests `a > b`.

## `switch`

Eight signal inputs, plus `clock`, rising-edge `reset`, zero-based `addr`, and
`n` 8 (`1..8`); output `out`. Config mode `seq` advances on clock; `addr`
rounds/clamps the live address. Example: `switch { mode: seq, n: 4 }`.

## `turing`

`turing { len: 8, prob: .1, seed: 1 }` — inputs `clock`, `write`, `clear`,
length `1..16`, mutation probability. Outputs normalized low-byte `out`,
one-sample incoming-bit `pulse`, and current `bit1..bit8` gates. Prob 0 locks a
loop; write forces a high incoming bit; clear zeros the whole register.

## `stepseq`

Configs `steps` CV array (default `[0]`), `gates` array (missing = on), `dir`
(`forward`, `reverse`, `pingpong`, `random`), `seed`. Inputs `clock`, reset,
`len` (0 = full array), transpose. Outputs held `cv`, one-sample `trig`,
clock-width `gate`, and forward/reverse wrap `eoc`.

```text
s = stepseq { steps: [0, .25, .58333], gates: [1, 0, 1] }
clock -> s.clock
s.cv -> osc.v_oct
s.trig -> env.trig
```

## `tempo`

Inputs `bpm` 120 and `beats` 1; outputs `seconds = 60 * beats / bpm` and `hz`.
Beat values: quarter 1, eighth .5, dotted eighth .75, sixteenth .25. Example:
`tempo { bpm: 120, beats: .75 }` produces .375 seconds.

## Recipes

### Euclidean variation

```text
clk = clock { bpm: 240 }
e = euclid { steps: 16, hits: 5, rot: 3 }
coin = bern { prob: .8, seed: 4 }
clk -> e.clock
e.trig -> coin.gate
coin.a -> drum_env.trig
```

### Repeatable generative melody

```text
t = turing { len: 8, prob: .1, seed: 12 }
q = quant { scale: [0, 2, 3, 5, 7, 9, 10] }
clock -> t.clock
t.out -> q.in : .4
q -> osc.v_oct
```

### Sidechain ducking

```text
m = mix { levels: [.8, .6] }
kick -> m.in1
pad -> m.in2
kick_env -> m.lvl2 : -.3
```
