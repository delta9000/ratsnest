# Granular — `grains`: clouds, freeze, stretch, glitch, shimmer

## When to reach for it

When you need texture you cannot compose note by note: a pad with no attack, a
held chord that outlives its source, a field-recording smear, a stutter. The
mental model is Clouds — a rolling buffer plus a scheduler that throws
overlapping windows at it — with the blend section left to cables, so dry/wet,
feedback, reverb and width are yours to patch.

`grains` is also the only pitch-shifter in the palette. There is no dedicated
shifter module; `pitch` on a grain player is how you fake one.

## Core patch

A plucked chord bed granulated into a slow-breathing wash.

```text
# Cloud pad.
clk = clock { bpm: 100 }
bar = div { by: 4 }
clk -> bar.in

src   = string { seed: 7, damping: 0.86, position: 0.3, freq: A2 }
chord = stepseq { steps: [0, 0.58496, 0.25, 0.75] }
clk -> src.strike
bar -> chord.clock
chord.cv -> src.v_oct

g = grains { secs: 2, seed: 11 }
src.out -> g.in : 0.9
0.12 -> g.position
0.06 -> g.scatter
0.3  -> g.size
18   -> g.density
0.25 -> g.jitter
0.6  -> g.shape
0.85 -> g.spread

drift = lfo { freq: 0.043hz, wave: sine }
drift -> g.position : 0.09

tone = svf { cutoff: 2200, res: 0.2 }
g.out_l -> tone.in
drift -> tone.cutoff : 600

verb = reverb { decay: 0.82, damp: 0.4, mix: 1 }
tone.lp -> verb.in_l : 0.7
g.out_r -> verb.in_r : 0.7

m = mix { levels: [0.6, 0.6, 0.5, 0.5], pans: [-0.7, 0.7, -1, 1] }
tone.lp     -> m.in1
g.out_r     -> m.in2
verb.out_l  -> m.in3
verb.out_r  -> m.in4
m.out_l -> out.left : 0.8
m.out_r -> out.right : 0.8
```

Two things make this a pad rather than mush. `position` sits at 0.12 — near
the newest audio, so the cloud tracks the current chord instead of smearing
four of them. And the buffer is 2 s, shorter than the chord block, for the
same reason. Long buffers are for scanning, not for pads.

## Variations

**Freeze as an event.** Freeze is a compositional device, not a mode. Gate it
rarely and irregularly and the texture becomes a photograph someone takes:

```text
roll = random { rate: 0.09hz, smooth: 0, seed: 21 }
frz  = logic { op: cmp }
roll -> frz.a
0.45 -> frz.b            # frozen roughly a quarter of the time
frz -> g.freeze
```

**Freeze relay.** One `grains` can hold *or* record, never both. Two can trade:
while A holds the last chord, B records the new one, and their level inputs
crossfade at the block boundary — the harmony moves and a frozen pad never
stops sounding. This is the centrepiece of `patches/permafrost.patch`:

```text
frz_a = stepseq { steps: [1, 0] }
frz_b = stepseq { steps: [0, 1] }
rel_a = stepseq { steps: [1, 0.3] }
rel_b = stepseq { steps: [0.3, 1] }
```

Freeze gates stay stepped (gates must be crisp); the *levels* slew through a
`func`. Note the first block: A freezes an empty buffer and is silent, so open
on B.

**Timestretch.** Low density, long grains, `position` crawling from newest to
oldest. A `func` with no trigger is the ramp — 55 s rise from a constant 1:

```text
g = grains { file: "loop.wav", seed: 8 }
scan = func { rise: 55, fall: 55 }
1 -> scan.in
scan -> g.position
0.5 -> g.size
6 -> g.density
0.1 -> g.jitter
0.75 -> g.shape
```

Pitch is untouched — this is time and pitch decoupled, the one thing granular
does that nothing else here can.

**Micro-glitch.** 5–20 ms grains, jitter at 1, scatter wide open. Below about
20 ms the window itself becomes the pitch and `size` reads as a formant:

```text
0.012 -> g.size
60 -> g.density
1 -> g.jitter
0.9 -> g.scatter
0 -> g.shape
1 -> g.spread
```

`shape: 0` (boxcar) keeps the clicks; raise it toward 1 (Hann) when you want
the same rhythm without the grit.

**Shimmer.** There is no pitch-shifter module. A grain player at `pitch: 1`
inside a reverb's feedback path *is* a shimmer reverb, and it is the reason
`grains` earns its place in an ambient patch:

```text
sh_g = grains { secs: 1.5, seed: 5 }
1 -> sh_g.pitch
0.2 -> sh_g.position
0.25 -> sh_g.size
30 -> sh_g.density
0.4 -> sh_g.jitter
0.9 -> sh_g.spread

verb = reverb { decay: 0.88, damp: 0.5, mix: 1 }
src.out -> verb.in_l : 0.5
src.out -> verb.in_r : 0.5
verb.out_l -> sh_g.in : 0.5
verb.out_r -> sh_g.in : 0.5
sh_g.out_l -> verb.in_l : 0.35
sh_g.out_r -> verb.in_r : 0.35
```

Each pass through the loop goes up another octave, so the tail brightens as it
decays. The return gain sets how many octaves you get before it dies; above
~0.5 with `decay: 0.88` it never dies. `pitch: 0.58496` instead of `1` gives a
fifth-stacking shimmer that turns into a cluster — use it deliberately.

**Per-grain modulation.** Every parameter latches at grain onset, so a clocked
`sh` gives each grain its own pitch and position. Set `density: 0` and drive
`trig` yourself:

```text
clk = clock { bpm: 480 }
r  = random { rate: 40hz, smooth: 0, seed: 9 }
pq = quant { scale: [0, 3, 5, 7, 10] }
r -> pq.in : 0.5
hold = sh { mode: sample }
pq -> hold.in
clk -> hold.trig

0 -> g.density
clk -> g.trig
hold -> g.pitch
```

Now the cloud is arpeggiating in the scale. Compositional granular, no new
module.

**Determinism twins.** Two instances with the same `seed`, the same input, and
the same freeze gate hold *identical* buffers. That is a free multi-head
player: one reads slow and low, the other fast and an octave up, both locked
to the same frozen moment.

```text
a = grains { secs: 2, seed: 3 }
b = grains { secs: 2, seed: 3 }
src.out -> a.in : 0.9
src.out -> b.in : 0.9
hold -> a.freeze
hold -> b.freeze
0.1 -> a.position
0.25 -> a.size
20 -> a.density
0.6 -> b.position       # same buffer, older audio
0.9 -> b.size
5 -> b.density
1 -> b.pitch            # ...an octave up
```

**File mode.** `file:` preloads a WAV, disables the record head, and makes
`in`/`freeze` inert. `position` then means start-of-file to end-of-file, and
there is no warm-up — sound from sample zero. This is the sampler-mangler
mode; combine with the timestretch ramp above.

## The knob that matters

`size` against `density`. Their product is the overlap, and overlap is the
difference between a cloud and a rhythm:

| size | density | result |
|---|---|---|
| 10 ms | 60 | stutter, grit, individual events audible |
| 100 ms | 20 | classic Clouds texture — grainy but continuous |
| 300 ms | 18 | pad; the source's pitch survives |
| 500 ms | 6 | timestretch; you hear the source, slowed |

Set `size` first from what you want each grain to *be*, then raise `density`
until the gaps close. `jitter` decides whether the residue sounds like a
machine (0, periodic) or weather (1, Poisson).

## Modulating it

- `position` under a 0.04–0.06 Hz sine at ±0.06..0.1 — the cloud breathes
  through the buffer. This one line is most of the life in the core patch.
- `size` under a slow LFO changes graininess without changing level.
- `spread` and a stereo `svf` on one channel only: the field opens as the
  texture thickens.
- `freeze` from an arrangement lane, not an LFO, once you have a form.
- `density` off an arrangement lane thickens a section — the loudness
  normalisation means it changes *texture*, not volume.

## Gotchas

- **A live buffer is silent for `position × secs` seconds**, not for `secs`.
  The ring starts zeroed and the read head trails the write head by
  `position`, so the warm-up scales with where you are reading from.
  Measured on a `secs: 4` cloud: `position: 0` sounds from sample zero,
  `0.25` from 1 s, `0.5` (the default) from 2 s, `1.0` not within 5 s.
  Keep buffers 1–4 s, drop `position` if you need an early start, or render
  long enough to see past it. `file:` mode has no warm-up.
  Remember `position`'s base is 0.5 — a `0.5 ->` cable makes it 1.0 and the
  cloud never speaks.
- Freezing an empty buffer freezes silence, and it stays silent until you
  unfreeze.
- **Output is loudness-normalised `1/√N`.** Turning `density` up will not make
  it louder. If the cloud is quiet, drive `g.in` harder (`: 1.4` is normal for
  a sparse plucked source) — `permafrost` sends at `: 2`.
- `size` is capped at a quarter of the buffer, silently. `size: 1` in a
  `secs: 2` buffer is really 0.5 s.
- Upward `pitch` is plain resampling: it aliases, exactly like the hardware.
  That is a texture, not a bug, but do not stack four octaves and expect
  clean.
- `position: 0` is the *newest* audio, 1 the oldest. Freezing never re-maps
  it.
- Beyond 64 concurrent grains, extra seeds drop silently — very high density
  with very long grains quietly stops getting denser.
- Feeding a moving melody into a buffer you then freeze gives you a chord you
  did not write. Freeze single-pitch sources, or size the buffer under one
  chord block (`patches/lib/frozen-cloud.sub` documents both).

## Where it shows up

Ambient and kosmische pads (`patches/granular.patch`,
`patches/lib/frozen-cloud.sub`, `patches/permafrost.patch`), dub techno
atmospheres (`patches/motorik.patch`), IDM/glitch percussion fills
(`patches/minor-machinery.patch`), shoegaze and post-rock washes, and any
"how did they get that pad" moment in modern ambient techno.

## Push it further

1. Put a `compressor` in the shimmer feedback path before the grain player,
   as the studio trick prescribes: the recirculating tail stays dense instead
   of thinning out into sparkle.
2. Granulate a `grains` output with a second `grains`. The second buffer's
   grains are made of grains; `size` on the outer instance becomes a rhythm
   generator.
3. Drive `trig` from a Euclidean pattern and `position` from a `stepseq`. That
   is a sampler with a sequenced playhead — write a melody out of one frozen
   second of audio.
4. Feed `onset` into a `func` into the reverb's `decay`. The space gets bigger
   exactly when grains arrive, which is physically wrong and sounds enormous.
5. Freeze a buffer of *noise*, not music. A frozen 2 s noise cloud at
   `size: 0.5` is a static, unchanging texture bed no LFO can imitate — and
   `seed` makes it the same bed every render.
