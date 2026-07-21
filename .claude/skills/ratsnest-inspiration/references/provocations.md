# The deck

Twenty cards. Draw one, apply it literally, and only then decide whether it
was a good idea.

Every card carries the number that makes it work. That is deliberate: a
provocation you can't execute is a provocation you'll nod at and ignore. If a
card doesn't change what you build, you didn't apply it — you agreed with it.

**Draw, don't choose.** If you find yourself scanning for the card that
endorses your existing plan, take the first one you dislike instead.

---

## Time

### 1. No two cycles may share a factor

Coprime lengths turn four small patterns into one long one. 5, 7, 11 and 13
have a lowest common multiple of **5005 steps** — at 96 BPM on a sixteenth
clock that is 13 minutes before the combination repeats. At 4, 8, 12 and 16 it
is one bar.

```text
e5  = euclid { steps: 5,  hits: 2 }
e7  = euclid { steps: 7,  hits: 3 }
e11 = euclid { steps: 11, hits: 4, rot: 2 }
e13 = euclid { steps: 13, hits: 5, rot: 5 }
clk -> e5.clock
clk -> e7.clock
clk -> e11.clock
clk -> e13.clock
```

### 2. Two clocks, one BPM apart

At 119 and 120 BPM the beats drift 4.2 ms per beat, pass through full
opposition at about 30 s, and realign after **119 beats — just under a
minute**. Long enough to sound like a mistake, short enough to resolve.

```text
a = clock { bpm: 120 }
b = clock { bpm: 119 }
```

Give each its own voice and never cable them together.

### 3. One saw, five minutes, no grid

At `phase: 0` a `saw` LFO starts at **−1** and rises monotonically to +1
(`tri` and `square` start at +1, `sine` at 0). One saw at 0.0033 Hz is a
303-second one-way ramp — the cheapest "one thing changes slowly" in the
language. Delete your arrangement lanes and use it.

```text
arc = lfo { wave: saw, freq: 0.0033hz }
arc -> filt.cutoff : 2400
arc -> hall.decay : 0.2
```

---

## Pitch

### 4. Five notes. Not six.

Randomness sounds like a fault through a chromatic quantizer and like
composition through a pentatonic one. Shrink the scale before you touch
anything else.

```text
q = quant { scale: [0, 3, 5, 7, 10] }
```

### 5. Just intonation only

Tempered intervals beat; just intervals lock and go still. Use only these and
let the drone stop moving.

```text
0.58496 -> fifth.v_oct     # 3/2
0.32193 -> third.v_oct     # 5/4
0.80735 -> seventh.v_oct   # 7/4
```

### 6. Detune in hertz, not in cents

Compose the beat rate as a tempo. At 110 Hz, `0.01306` v/oct is exactly 1 Hz
of detune — a one-second pulse. `0.00328` is 0.25 Hz — a four-second swell.

```text
a = vco { freq: 110hz, wave: tri }
b = vco { freq: 110hz, wave: tri }
0.00328 -> b.v_oct
```

### 7. Put the bass above the pad

Nothing technical. Voice the bass an octave up and the pad an octave down and
find out which one you were actually listening to.

---

## Timbre

### 8. Brightness without a level change

A filter costs you level; a wavefolder does not. Measured on a triangle at
unity, moving `fold` from 1 to 8 shifts the energy above 2 kHz relative to
below 500 Hz by **+40.6 dB** while total RMS stays pinned at −4.8 dBFS.

```text
t = vco { freq: 220hz, wave: tri }
f = fold { fold: 4.5 }                  # base is the centre, not the floor
breathe = lfo { wave: sine, freq: 0.024hz }
t -> f.in
breathe -> f.fold : 3.5                 # sweeps 1 .. 8
```

Use this wherever you were about to automate a cutoff.

### 9. The pluck that never stops

`string` damping is not a switch at 0.95. Measured decay after one strike at
110 Hz: **0.95 loses ~4 dB/s** (dead in 20 s), **0.99 loses ~1.2 dB/s**, and
only **1.0 is lossless** (1.5 dB across 18 seconds). Set it to 1.0 and you
have a drone, not a note.

```text
s = string { seed: 1, damping: 1.0, freq: 110hz }
```

### 10. Modulate an operator, not a filter

`fm`'s `lvl1..lvl6` are **inputs**. Timbre is an automation target; you do not
need a filter to make an FM voice move.

```text
drift = lfo { wave: tri, freq: 0.0071hz }
drift -> ep.lvl3 : 0.35
```

### 11. No `vco` anywhere

Only `modal`, `string`, `noise` and `grains`. Struck resonators sit around
−25 dBFS RMS with 23 dB of crest against a saw's −4.8 and 4.7 — so you will
also have to relearn how to level a mix. That is the point.

### 12. Make up the ratios

`modal` will take any mode placement you invent. List the fundamental first;
everything after is your instrument.

```text
bell = modal { ratios: [1, 2.76, 5.40, 8.93, 13.34, 18.64], damping: 0.9 }
```

---

## Space

### 13. Three milliseconds is not an echo

At `time: 0.003` a delay is a comb ringing at 1/t = **333 Hz**. Verified:
energy peaks at 333, 666 and 1000 Hz with nulls at 166 and 500. Excite it with
noise and you have a pitched resonator you never tuned.

```text
n = noise { color: white, seed: 1 }
c = delay { time: 0.003, feedback: 0.85, mix: 1, tone: 9000, slew: 0.001 }
n -> c.in : 0.15
dc = dcblock { cutoff: 20hz }
c -> dc.in_l
```

`dcblock` is not optional above ~0.9 feedback.

### 14. The dry signal is a rumour

Put the source at 0.15 and the returns at 0.6. A wet-only `reverb` or `delay`
comes back at roughly source level (measured −3.4 and −3.1 dBFS from a unity
saw), so this is a real inversion, not a trim.

```text
stab -> m.in1 : 0.15
plate.out_l -> m.in5 : 0.6
plate.out_r -> m.in6 : 0.6
```

### 15. Everything mono until the last two modules

No panning, no per-voice stereo, no chorus. Width is born in the plate and
nowhere else. Sum, filter, then let one reverb open it up.

### 16. Automate the room, not the send

`reverb.decay` under a glacial LFO changes what the space *is*; a send level
only changes how much of it you hear.

```text
tide = lfo { wave: sine, freq: 0.0059hz }
tide -> hall.decay : 0.18
```

---

## Process

### 17. Detuning buys RMS headroom and never peak headroom

Measured on eight saws at 0.11 each: identical pitch sums **N×** (+18.1 dB at
n=8), detuned sums **√N×** (+8.7 dB) — but peak is **−1.2 dBFS either way**.
Stage a unison stack on its peak, not on how loud it feels.

### 18. `drive` is a limiter

`drive { amount: 0.5 }` on a saw takes crest from 4.7 dB to **0.9 dB** and
adds 3.9 dB of RMS. If you wanted glue, you want `overdrive { mix: 0.5 }` or
the master clipper.

### 19. Change one seed, keep the surprise

Seeded modules are exactly repeatable, so a patch is a family, not a take.
Render both and keep the one you did not intend.

```text
t = turing { len: 8, prob: 0.06, seed: 12 }   # then 29, then 47
```

### 20. Take the best-sounding element out

Not the worst. Render without it and sit with what's left before you decide.

---

## If none of them bite

The problem is upstream of the deck. In order:

1. You cannot say in one sentence what the piece is about.
2. You have modulated levels but not timbres, so nothing is *becoming*
   anything.
3. The render is too short — glacial LFOs have done nothing at 30 seconds.
4. You are adding when you should be subtracting.
