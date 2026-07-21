# Noise texture — wind, rain, hiss, crackle, howl, fake field recordings

## When to reach for it

When a patch is technically correct and sounds like a demo. Noise beds are the
cheapest way to make a mix sound like it was *recorded* somewhere: a room, a
tape, a storm, a broken speaker. They also carry arrangement — a texture that
opens up under a drop does more than another synth layer.

The recipe never changes: **a noise source, a resonance that shapes it, and
slow motion on the resonance**. Static noise is wallpaper. Moving noise is
weather. Everything below is that sentence with different filters.

## Core patch

Wind. Pink noise through a resonant bandpass driven by three incommensurate
rates, plus a dull low body underneath so it has weight.

```text
# Wind.
n = noise { color: pink, seed: 4 }

gust  = lfo { freq: 0.037hz, wave: sine }
sway  = lfo { freq: 0.011hz, wave: tri }
flick = random { rate: 0.9hz, smooth: 1, seed: 12 }

bp = svf { cutoff: 480, res: 0.86 }
n -> bp.in : 0.6
gust  -> bp.cutoff : 320
sway  -> bp.cutoff : 180
flick -> bp.cutoff : 90
0.09 -> bp.res
sway -> bp.res : 0.06

body = svf { cutoff: 900, res: 0.4 }
n -> body.in : 0.5
gust -> body.cutoff : 500

amp = vca { gain: 0.3 }
bp.bp -> amp.in : 0.5
body.lp -> amp.in : 0.12
gust -> amp.cv : 0.25
flick -> amp.cv : 0.12

verb = reverb { decay: 0.7, damp: 0.6, mix: 0.5 }
amp -> verb.in_l
amp -> verb.in_r : 0.85
verb.out_l -> out.left : 0.9
verb.out_r -> out.right : 0.9
```

Three details do the work. The rates 0.037 / 0.011 / 0.9 Hz never realign, so
the gusting never loops. The *same* LFO drives cutoff and level — real wind
gets louder as it gets brighter, and moving them independently sounds fake.
And the reverb sends are asymmetric (`0.85` on the right), which decorrelates
a mono source without a `stereo` module.

## Variations

**Rain and static.** A high sheet plus discrete droplets. The sheet is
high-passed white with fast smoothed random on its level; the drops are a
resonant `svf` pinged by rare Bernoulli hits at random pitches:

```text
n = noise { color: white, seed: 2 }
hiss = svf { cutoff: 2400, res: 0.15 }
n -> hiss.in : 0.5
flick = random { rate: 11hz, smooth: 0.35, seed: 17 }
sheet = vca { gain: 0.18 }
hiss.hp -> sheet.in
flick -> sheet.cv : 0.07

drops = clock { bpm: 1800 }
coin = bern { prob: 0.12, seed: 5 }
drops -> coin.gate
denv = func { rise: 0.0004, fall: 0.018, curve: exp }
coin.a -> denv.trig
dpitch = random { rate: 30hz, smooth: 0, seed: 41 }
drop = svf { cutoff: 2600, res: 0.9 }
n -> drop.in : 0.5
dpitch -> drop.cutoff : 1400
damp = vca { gain: 0 }
drop.bp -> damp.in
denv -> damp.cv : 0.5
```

**Tape hiss bed.** Pink noise, high-passed around 1 kHz, sitting 35–45 dB
under everything, with a barely-there level wobble. Two `noise` modules with
*different* seeds for left and right — same seed means an identical stream,
which is mono and defeats the point.

```text
hl = noise { color: pink, seed: 21 }
hr = noise { color: pink, seed: 22 }
hpl = svf { cutoff: 1200 }
hl -> hpl.in : 0.25
```

**Vinyl crackle.** Rare gated clicks, not a filter sweep. A fast clock through
a low-probability `bern`, a 6 ms exponential decay, white noise as the click
body, bandpassed so it is paper and not fizz:

```text
fast = clock { bpm: 2400 }
coin = bern { prob: 0.04, seed: 33 }
fast -> coin.gate
tick = func { rise: 0.0002, fall: 0.006, curve: exp }
coin.a -> tick.trig
n = noise { color: white, seed: 8 }
cr = vca { gain: 0 }
n -> cr.in
tick -> cr.cv : 0.9
crf = svf { cutoff: 3200, res: 0.3 }
cr -> crf.in
```

`prob` is the record's condition: 0.02 is a good pressing, 0.15 is a car boot
sale. Modulating `prob` from an arrangement lane makes the vinyl degrade over
the track.

**Granular field-recording fake.** Granulating a filtered noise bed at low
density and downward pitch turns synthetic noise into something that sounds
captured — the grain windows impose an irregular, non-synthetic envelope:

```text
bp = svf { cutoff: 700, res: 0.7 }
n -> bp.in : 0.6
g = grains { secs: 2, seed: 9 }
bp.bp -> g.in : 0.9
0.45 -> g.size
9 -> g.density
0.8 -> g.jitter
0.5 -> g.scatter
1 -> g.shape
1 -> g.spread
-0.3 -> g.pitch
```

Downward `pitch` is the trick: resampling below unity darkens *and* stretches,
which is what a distant, badly recorded source sounds like.

**Resonant-feedback howl.** A delay of a few milliseconds at high feedback is
a tuned pipe, not an echo — reverb-as-resonator, the oldest trick in the
ambient book. Trickle noise in, sweep the delay time, and it sings:

```text
n = noise { color: pink, seed: 5 }
howl = delay { time: 0.0043, feedback: 0.97, mix: 1, tone: 3800, slew: 0.002 }
n -> howl.in : 0.2
sweep = lfo { freq: 0.023hz, wave: sine }
sweep -> howl.time : 0.0022
drift = random { rate: 0.07hz, smooth: 1, seed: 3 }
drift -> howl.time : 0.0009
dc = dcblock {}
howl -> dc.in_l
howl -> dc.in_r
```

4.3 ms ≈ 230 Hz. Halve the time to go up an octave. `slew` must stay tiny or
the time smoother turns the sweep to mud; `tone` sets how many harmonics
survive each pass, and it is the difference between a flute and a scream.
`dcblock` afterwards is not optional at this feedback.

**Reactive texture.** `follow` turns any part into a control source, which is
how a bed stops feeling like it was pasted on. Here the noise brightens and
swells on each kick:

```text
f = follow { mode: peak, attack: 4ms, release: 260ms }
kick.main -> f.in
air = svf { cutoff: 600, res: 0.5 }
n -> air.in : 0.5
f.env -> air.cutoff : 2600
amp = vca { gain: 0.05 }
air.bp -> amp.in
f.env -> amp.cv : 0.5
```

Use `mode: rms` and a 1 s release to follow a *pad* instead — the bed then
breathes with the harmony rather than the drums.

## The knob that matters

Filter resonance, and how fast you move the cutoff. `svf.res` under about 0.5
is a colour; between 0.7 and 0.95 the filter starts to have a pitch, and the
noise stops being noise and becomes an object with a size. Wind, howls, and
bottles all live above 0.8. Rain and hiss live below 0.3.

Then: **cutoff motion rate.** Under 0.05 Hz reads as environment. Around
1–10 Hz reads as material (rain, fire, gravel). Above that it is modulation
and the listener hears a synth.

## Modulating it

- Three or four LFOs at coprime, unrelated periods (0.011 / 0.037 / 0.9 Hz)
  summed onto one cutoff. Never one LFO.
- Cutoff and level from the same source, at different depths. Brightness and
  loudness co-vary in the real world.
- `random { smooth: 1 }` for the irregular component — an LFO alone is too
  regular for anything organic.
- `bern.prob` and `func.fall` from arrangement lanes: crackle density and
  droplet length are texture automation that costs no new voices.
- `noise` colour cannot be modulated (it is a config), so crossfade two
  `noise` modules through `xfade` when you want the colour to move.

## Gotchas

- **Two `noise` modules with the same `seed` are the same signal.** Panning
  them apart gives you a centred mono bed with extra steps.
- High `svf.res` plus a hot input clips fast, and the config value is a base
  that cables sum onto — `res: 0.86` plus a `0.09` cable is 0.95, not 0.09.
- Beds are quieter than you think. Aim for a bed 25–40 dB under the mix; if
  you can clearly hear it as a separate element, it is 10 dB too loud.
- `delay` feedback above 1 self-oscillates and will keep growing. 0.97 with a
  `tone` roll-off is stable; 0.99 is not, without a limiter.
- White noise fed straight into a reverb eats the whole tank. Bandpass first.
- `follow` has no sensitivity control — scale the source at the cable, and
  remember `env` is unclamped above 1 for hot inputs.
- A crackle patch that checks fine can still be silent if the `bern`
  probability and clock rate never coincide with your render window. Verify
  RMS, not just `check`.

## Where it shows up

Dark ambient and drone (`patches/dark-ambient.patch`,
`patches/cathedral.patch`), lo-fi and dub techno hiss beds
(`patches/dub-techno.patch`, `patches/motorik.patch`), the noise layer inside
every snare and hat (`patches/lib/kit/`), industrial and power-electronics,
and the between-track air of any album-length render.

## Push it further

1. Send the wind bed through the resonant howl at 5% and let the gusts excite
   the pipe — the storm plays the building.
2. Freeze a `grains` buffer full of noise. A frozen noise cloud is a fixed,
   unchanging texture that no LFO can imitate, and `seed` makes it identical
   every render.
3. Put `follow` on the *noise bed* and duck the pad with it. The texture
   becomes the rhythm section.
4. Drive the crackle clock from the same divider as the hats. Sub-audible
   rhythmic coherence in the noise floor is felt, not heard.
5. Replace the `svf` in the wind patch with a `modal` at high `damping` and
   low `structure`. Now the wind has a body it is blowing through, and moving
   `modal.freq` is moving the size of the room.
