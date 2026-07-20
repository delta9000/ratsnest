---
name: ratsnest-generative
description: Use when a ratsnest patch should play itself — generative, self-playing, evolving, never-repeating, ambient, drone, endless, or "set it running" music — or when a generative patch sounds random rather than composed, loops audibly, or goes nowhere.
---

# Generative systems in ratsnest

A generative patch is not a random patch. It is a set of periodic and stochastic
processes whose *ranges* you composed, running against each other on periods that
don't line up. Randomness supplies novelty; the ranges supply the music.

Load `ratsnest-patching` first. `docs/research/2026-07-20-modular-ambient-technique.md`
is the source material behind this skill.

## The five primitives

Every generative technique in the literature reduces to these. Reach for them in
this order.

1. **Incommensurate periods.** Several free-running cycles whose ratios are not
   small integers, so the combination never audibly repeats.
2. **A locked ↔ fresh axis.** One knob between "repeat exactly" and "all new" —
   and then modulate that knob slowly, so the *character* of the randomness drifts.
3. **Strike-then-resonate.** Excite a resonator and let it decay. Struck events
   read as *things happening*; sustained tones into a filter do not.
4. **Beating.** Two near-identical pitches. The difference frequency is a tempo
   you can compose.
5. **Feedback as an instrument.** Delay and reverb with their own recirculating
   paths, not terminal sends.

## 1. Incommensurate periods

**Modulation.** Four or five glacial `lfo`s with prime-ish periods:

```text
d1 = lfo { wave: sine, freq: 0.0189hz }    #  53 s
d2 = lfo { wave: tri,  freq: 0.0114hz }    #  88 s
d3 = lfo { wave: sine, freq: 0.0071hz }    # 141 s
d4 = lfo { wave: tri,  freq: 0.0033hz }    # 303 s
```

**Waveform choice sets the shape of the arc**, and the start phase is not
obvious: at `phase: 0` a `saw` starts at −1, `tri` and `square` at +1, `sine`
at 0. So one `saw` at 0.0033 Hz is a clean monotonic −1 → +1 ramp across five
minutes — the cheapest "one thing changes slowly" there is. A `tri` at the
same rate travels the wrong way first.

Route each to a *different* destination — filter cutoff, `fm` operator level,
`delay.feedback`, `reverb.decay`, `grains.position`, `fold.fold`, a pan. Never
two to the same destination; that just makes a slower LFO.

**Rhythm.** Coprime `euclid` step counts on one clock. Two layers at 5 and 7
steps realign every 35 bars; at 4 and 8 they realign every bar and sound like a
loop.

```text
e1 = euclid { steps: 5,  hits: 2 }
e2 = euclid { steps: 7,  hits: 3 }
e3 = euclid { steps: 11, hits: 4 }
clk -> e1.clock
clk -> e2.clock
clk -> e3.clock
```

**Melody.** `stepseq` lanes of coprime length against each other: a 5-step pitch
sequence and a 7-step gate sequence give a 35-step phrase from twelve numbers.
Set `len:` at runtime from a lane and the phrase length becomes an automation
target.

The oldest version of this is Eno's *Music for Airports*: tape loops of 20.1 s
and 19.6 s, chosen so "they are not likely to come back into sync again."

## 2. Locked ↔ fresh

`turing` is the whole idea in one module. `prob: 0` locks a loop forever;
`prob: 0.5` is pure noise; small values mutate slowly.

```text
t = turing { len: 8, prob: 0.06, seed: 12 }
q = quant { scale: [0, 3, 5, 7, 10] }
clk -> t.clock
t.out -> q.in : 0.4
q.out -> voice.v_oct
```

**Then modulate the probability.** This is the move most patches miss:

```text
mutate = lfo { wave: tri, freq: 0.0043hz }   # 233 s
mutate -> t.prob : 0.08
```

Now the riff locks for minutes, dissolves, and re-forms as something related.
`turing.bit1..bit8` are free gate streams derived from the same register — use
them for percussion so the rhythm and the melody share a genome.

`bern` is the per-event version: a coin flip that routes each trigger to `a` or
`b`. Use it to thin a clock into sparse events, to alternate between two voices
without mechanical alternation, or to decide accents.

```text
coin = bern { prob: 0.35, seed: 4 }
clk -> coin.gate
coin.a -> bell.strike        # ~35% of clocks become a bell
coin.b -> chime.strike
```

`random { smooth: 1 }` is a wandering CV; `random.stepped` is its sample-and-hold
twin. `sh` sampling a `random` on a clock gives you the classic
random-melody-per-step, and `sh { mode: track }` gives you a gated smear.

## 3. Strike-then-resonate

An ambient patch with no events is a wash. Events are what make it a piece.

```text
# sparse bell: a coin-gated slow clock striking a modal resonator
slow = div { by: 12 }
clk -> slow.in
coin = bern { prob: 0.4, seed: 9 }
slow -> coin.gate

pitch = sh { mode: sample }
wander = random { rate: 0.3hz, smooth: 1, seed: 3 }
wander -> pitch.in
slow -> pitch.trig
qq = quant { scale: [0, 2, 5, 7, 9] }
pitch -> qq.in : 0.5

bell = modal { modes: 48, structure: 0.6, brightness: 0.4, damping: 0.86 }
qq.out -> bell.v_oct
coin.a -> bell.strike
```

`lpg` is the vactrol version — `strike` gives the soft attack and non-linear
decay that a `vca` + envelope cannot. `string` with high `damping` crosses toward
infinite sustain and becomes a drone rather than a pluck — but the crossfade
only *begins* at 0.95; at 0.99 an A1 still loses ~0.8 dB/s, and only
`damping: 1.0` is genuinely lossless (measured flat over 20 s). Both `modal` and
`string` also accept audio on `in`: a slow `noise -> vca -> m.in` is a bowed or
blown texture instead of a struck one.

## 4. Beating

```text
a = vco { freq: 110hz, wave: tri }
b = vco { freq: 110hz, wave: tri }
0.0033 -> b.v_oct                       # ~0.25 Hz beat: a 4-second pulse
```

Beat rate = frequency difference. Choose it as a tempo. Éliane Radigue built
whole pieces on ARP 2500 oscillators tuned within a fraction of a Hz.

Make the beat itself drift by putting a very slow `random` on one oscillator's
`v_oct` at depth ~0.002. Now the drone breathes irregularly instead of pulsing.

For a bed, stack **octaves and fifths**, not thirds — the open interval lets a
melody imply the mode. `patches/lib/sym-drone.sub` is a ready-made coupled-string
drone with `root`/`fifth` inputs.

## 5. Feedback as an instrument

```text
# a delay whose feedback and tone are themselves modulated
ech = delay { time: 0.617, feedback: 0.62, mix: 1, tone: 3200, slew: 0.25 }
d2 -> ech.feedback : 0.22          # 0.40 .. 0.84 over 88 s
d3 -> ech.tone : 1800
src -> ech.in : 0.5

# reverb decay as a slow instrument, with occasional freeze
sp = reverb { decay: 0.8, damp: 0.35, mix: 1 }
d4 -> sp.decay : 0.18
ech -> sp.in_l : 0.5
ech -> sp.in_r : 0.5
```

Feed a delay's output back into a `reverb` and the reverb back into the delay at
low gain and you have a dub ecosystem that never settles. Watch the `--stats`
peak: a feedback net can bloom into clipping ten minutes in, long after the
window you rendered.

**Freeze** is the granular version of the same idea. `grains.freeze` halts
recording while grains keep reading — a moment captured and held. Gate it from a
slow, rare source rather than a metronome. `patches/lib/frozen-cloud.sub`
packages this, and `patches/permafrost.patch` runs two of them as a relay so one
always holds the previous chord while the other records the next.

## Making it sound composed, not random

| Symptom | Fix |
|---|---|
| Sounds like a fault, not a melody | shrink the `quant` scale — 5 notes, not 12 |
| Pleasant but nothing happens | add events (strike-then-resonate), not more drift |
| Audibly loops | your periods share a small common multiple; make one coprime |
| Chaotic, no identity | lock something: `turing { prob: 0 }`, or a fixed pentatonic phrase |
| Boring after 90 seconds | modulate a *modulation depth*, not just a parameter |
| Everything moves at once | stagger the LFO periods across an order of magnitude |

**Set the range, not the value.** Bias the destination to the darkest/quietest
version you'd accept, then use cable depth to define how far it may travel. You
are composing the boundaries of a behaviour, not a setting.

**Constrain the palette, then let the machine loose inside it.** Two mid/upper
voices and one low voice, one key, one scale. The quantizer is the guardrail
that makes randomness sound intentional.

**Ratsnest can go further than hardware.** Seeds make every random stream
repeatable, so a generative patch is reproducible — you can render it twice and
get the same take, then change one seed and get a sibling. Two `grains`
instances fed identical input hold identical buffers. Use that: relay freezes,
multi-player-per-buffer textures, A/B voices that diverge only in seed.

## Skeleton: a self-playing patch

```text
# 1. one clock, a div chain, and coprime euclids off it
# 2. one turing -> quant -> pitch bus, prob under slow LFO
# 3. two or three voices: one drone (beating/sym-drone),
#    one struck (modal/string/lpg), one textural (grains/noise)
# 4. four glacial LFOs on four different destinations
# 5. a delay+reverb feedback ecosystem, wet-only on their own channels
# 6. no arrangement grid; fade in post
```

Render 6–10 minutes and read the `--stats` timeline: you want RMS that moves
±3 dB on no discernible schedule, and no window identical to another.
