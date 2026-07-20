# IDM / Braindance — the process makes the pattern, the tape ruins it

## The feel

Nothing is drawn in; everything is *derived*. A rule-based system emits the
rhythm, a second system perturbs it, and then the whole thing is dragged
through a degraded playback medium so it never sounds like a computer that is
sure of itself. Two opposing forces, always: a locked, obviously-designed core
(`prob: 0` turing, fixed ratios, a repeating bell figure) and a layer of
instability on top (jitter, wow, grain scatter).

If it sounds random, you have only built the second half. If it loops
audibly, you have only built the first.

## Clock

Any tempo — 85, 104, 160, or none. The distinctive move is that the clock
itself is not perfectly stable.

```text
master = clock { bpm: 432 }              # 108 BPM sixteenths
drift  = random { rate: 0.05hz, smooth: 1, seed: 2 }
drift -> master.bpm : 6                  # ±6 BPM over ~20 s: tape, not rubato
```

Keep the `div` chain off `master` as usual; everything drifts together, which
is what a tape machine does and a swing setting does not.

## Palette

| Element | Modules | Starting values |
|---|---|---|
| rhythm generator | `turing` + `logic` + `bern` | `len: 12, prob: 0.06`, read `bit1..bit8` |
| locked harmony | `turing { prob: 0 }` + `quant` | `len: 8`, scale `[0,2,3,5,7,10]` |
| pitched voice | `modal` / `string` | `structure: 0.55`, `damping: 0.7` |
| glitch | `grains` | `size: 0.012`, `density: 45`, `jitter: 0.95` |
| tape wow | `random` + `lfo` -> `delay.time` | 0.17 Hz ±2.5 ms, 6.3 Hz ±0.4 ms |
| bitcrush | `sh` clocked by an audio-rate `lfo` | `lfo { freq: 5200hz, wave: square }` |
| kit | `lib/kit/*.sub` | short decays; let the sequencer supply the drama |

## Signature moves

### 1. One turing register is eight rhythm lanes

`turing.bit1..bit8` are the register's current bits as gates. Read different
bits for different voices and you get patterns that are *related* — they share
a substrate, drift together when a bit mutates, and lock when `prob: 0`.

```text
t = turing { len: 12, prob: 0.06, seed: 23 }
master -> t.clock

kick = sub { file: "lib/kit/kick.sub" }
rim  = sub { file: "lib/kit/rim.sub" }
hat  = sub { file: "lib/kit/hat.sub", decay: 0.08 }

kgate = logic { op: and }       # kick only where two bits agree: sparse, related
t.bit1 -> kgate.a
t.bit4 -> kgate.b
kgate -> kick.trig

coin = bern { prob: 0.4, seed: 9 }
t.bit3 -> coin.gate
coin.a -> rim.trig              # the same lane, split two ways —
coin.b -> hat.trig              # never simultaneous, always accounted for
```

`len: 12` against a 16-step feel is the polymetric trick: the pattern
repeats every 3 bars instead of every bar. Coprime lengths against coprime
`euclid { steps: }` is the same idea one level up.

### 2. Humanize by making the trigger *late*, not early

There is no negative delay. Route the trigger through a `func` whose rise time
is jittered per hit and fire the voice from `eor` (end of rise). Each hit
lands 2–18 ms behind the grid, by a different amount.

```text
jit    = random { rate: 8hz, smooth: 0, seed: 4 }
jit_sh = sh { mode: sample }
jit.stepped -> jit_sh.in
master -> jit_sh.trig
late = func { rise: 0.010, fall: 0.002 }
t.bit6 -> late.trig
jit_sh -> late.rise : 0.008       # bipolar: 0.010 ± 0.008 s
late.eor -> hat.trig
```

Use different seeds per voice or the whole kit lurches as one, which reads as
a bad clock rather than a played one. This is the single move that separates
IDM percussion from a drum machine.

### 3. Granular glitch: tiny grains, Poisson scatter, per-grain pitch

`size` below ~20 ms puts you under the pitch-perception threshold, so the
grain rate becomes the timbre. `jitter: 0.95` makes the stream Poisson — the
mean density holds but nothing is periodic.

```text
src = sampler { file: "samples/pad.wav", loop: on }
gl  = grains { secs: 4, seed: 12, size: 0.012, density: 45, jitter: 0.95,
               shape: 0.35, spread: 0.8 }
src -> gl.in
pj = sh { mode: sample }
jit.stepped -> pj.in
gl.onset -> pj.trig               # every grain onset draws a new pitch
pj -> gl.pitch : 0.6              # ±0.6 oct, latched at onset
```

`grains` latches every parameter at grain onset, so `onset -> sh.trig -> pitch`
is a legitimate per-grain modulation loop, not a race. Freeze the buffer
(`freeze` gate high) for a block and the source can leave entirely.

### 4. Sample-rate reduction with `sh`

There is no bitcrusher. A sample-and-hold clocked by an audio-rate square
*is* a downsampler, aliasing and all.

```text
crush_clk = lfo { freq: 5200hz, wave: square }
crush     = sh { mode: sample }
gl.out_l  -> crush.in
crush_clk -> crush.trig
```

Sweep `crush_clk.freq` from 20 kHz down to 2 kHz across a section and the
material dissolves without a filter being involved.

### 5. Tape: wow is slow and random, flutter is fast and periodic

Two modulators, one short delay line, `mix: 1`, no feedback. This is the
Boards of Canada layer — apply it to a bus, not to one voice.

```text
wow  = random { rate: 0.17hz, smooth: 1, seed: 31 }
flut = lfo { freq: 6.3hz, wave: tri }
tape = delay { time: 0.006, feedback: 0, mix: 1, slew: 0.0008, tone: 9000 }
wow  -> tape.time : 0.0025
flut -> tape.time : 0.0004
```

Keep `slew` near zero or the delay's own smoother eats the modulation. The
same `wow` signal on a `sampler.pitch : 0.01` is the tape-speed knob instead
of the tape path; use one or the other, not both, or the pitch wobbles twice.

### 6. Lock the harmony while the rhythm mutates

```text
lock = turing { len: 8, prob: 0, seed: 41 }    # prob 0 = a fixed 8-note loop
q    = quant { scale: [0, 2, 3, 5, 7, 10], root: 2 }
master -> lock.clock
lock.out -> q.in : 0.5
bell = modal { modes: 48, structure: 0.55, brightness: 0.6, damping: 0.7 }
q.out -> bell.v_oct                            # quant's output is `out`
t.bit2 -> bell.strike
```

## Arrangement

```text
# Arrangement (no grid required; if you use one, 8 blocks of 8 bars):
#   0     tape-degraded pad alone, no pulse
#   1     turing enters at prob 0 — a pattern you can learn
#   2-3   prob rises to 0.06; the pattern starts eroding
#   4     rhythm drops out, granular layer takes the foreground
#   5-6   both; humanization depth doubles; SR-reduction sweeps down
#   7     everything gone but the wow-modulated pad, drifting flat
```

IDM tolerates — wants — a form that is not powers-of-two. See
`ratsnest-composition/references/form.md` ("the infinite scene") and
`ratsnest-generative` for incommensurate-period scheduling.

## Mix targets

- Drop-window RMS ≈ −16 dBFS, crest ~16 dB. Do **not** master this to club
  loudness; the dynamics are the genre
  (`ratsnest-composition/references/mixing.md`).
- Percussion transients want headroom: kick channel ~0.8, everything else well
  under, premaster peak ≈ −3 dBFS.
- High-pass the granular layer at 120 Hz — tiny grains generate low-frequency
  garbage from window discontinuities.
- Noise floor is content. A `noise { color: pink }` at 0.02 under the whole
  mix is the tape, and it costs you nothing.
- Watch DC: `math { op: abs }`, folders, and pulse at `pw != 0.5` all offset.
  `dcblock` before the bus.

## In this repo

- `patches/shadow-lattice.patch`, `patches/permafrost.patch` — generative
  scheduling from coprime periods; steal the LFO-period arithmetic.
- `patches/granular.patch` — the `grains` parameter space, annotated.
- `patches/strum.patch` — `turing -> quant -> alloc` into a voice group; the
  cheapest way to get three ringing voices out of one mono lane.
- `.claude/skills/ratsnest-generative/SKILL.md` — "locked ↔ fresh" is the
  IDM thesis stated abstractly.

## How to break it

1. **Set `prob: 0` and leave it there for the whole track.** Let the "erosion"
   come entirely from the tape layer. Autechre's early records are far more
   fixed than their reputation.
2. **Humanize the *pitch* instead of the timing.** `jit_sh -> osc.v_oct :
   0.004` — under two cents, per note. It stops sounding like a synthesizer
   and you cannot say why.
3. **Put the granular layer *under* the drums, not over them.** Same material
   as the pad, `size: 0.6`, `density: 3` — a smeared shadow, not a glitch.
4. **Let one voice be perfectly quantized.** The contrast is the whole point;
   if everything is late, nothing is.
5. **Run the drum bus through `tape` with `feedback: 0.35`.** Not a wash —
   a 6 ms comb that turns every hit slightly metallic and pitched.

## Listening

- Aphex Twin, *Xtal* — a locked loop plus tape instability; almost no
  generative content. Steal the restraint.
- Aphex Twin, *Windowlicker* — sample-accurate micro-edits on a vocal; that
  is `grains` with `density: 0` and an external `trig` lane.
- Autechre, *Gantz Graf* — parameter automation *as* the composition; every
  timbral change is a modulator, not a new sound.
- Autechre, *Bike* — coprime pattern lengths audibly phasing. `turing
  { len: 12 }` against 16 is this.
- Boards of Canada, *Roygbiv* — the wow/flutter recipe above, plus detuned
  layers. Note how little glitch is actually present.
- Boards of Canada, *Olson* — degradation applied to a *pretty* source. The
  technique is only interesting over material that would survive without it.
- Squarepusher, *My Red Hot Car* — timing humanization pushed to the point of
  a stumble, held together by a rigid bass.
- Plaid, *Eyen* — melodic IDM: quantized, locked harmony under an eroding
  rhythm. The exact split in move 6.
