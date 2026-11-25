# Physical modelling — `modal` and `string`: struck, bowed, blown, coupled

## When to reach for it

When you want a voice that *responds* instead of one that plays. A resonator
has no envelope of its own — you excite it and the decay is a property of the
body, so hard hits ring differently from soft ones and nothing sounds like a
sample. Reach for `string` for anything plucked, bowed, or droning; `modal`
for anything struck that has a body — bells, bars, drums, plates, bowls, and
the whole `patches/lib/kit/` drum family.

For an actual piano, don't build one from `string`: use `piano` (one voice per
note, in a `voice` group) into one shared `soundboard`. It models the hammer,
unisons, dampers and pedal itself; `patches/piano/` has a MIDI patch, a
keyboard sweep and a pedalled nocturne. Its `vel` base is 0, and `alloc`'s and
`chord`'s is 1, so declare those `vel: 0` when a tracker owns velocity.

Excitation → resonance → shaping, with the shaping *inside* the resonance:
`damping` is the envelope. That is the whole reason these voices sound alive.

## Core patch

Struck string with two sympathetic partners — the Rings voice, complete.

```text
# Struck string + sympathetic pair.
clk  = clock { bpm: 96 }
hits = euclid { steps: 8, hits: 3 }
mel  = turing { len: 8, prob: 0.06, seed: 5 }
key  = quant { scale: [0, 2, 3, 7, 10] }
clk -> hits.clock
clk -> mel.clock
mel.out -> key.in : 0.4

main = string { seed: 7, damping: 0.72, position: 0.28, structure: 0.25, freq: A2 }
key -> main.v_oct
hits.gate -> main.strike

symp1 = string { seed: 11, damping: 0.88, position: 0.4, freq: A2 }
symp2 = string { seed: 13, damping: 0.9,  position: 0.2, freq: A2 }
key -> symp1.v_oct
0.58496 -> symp1.v_oct
key -> symp2.v_oct
1 -> symp2.v_oct
main.out -> symp1.in : 0.1
main.out -> symp2.in : 0.1

body = mix { levels: [1, 0.55, 0.4], pans: [0, -0.5, 0.5] }
main.out  -> body.in1
symp1.out -> body.in2
symp2.out -> body.in3

verb = reverb { decay: 0.7, damp: 0.35, mix: 0.35 }
body.out_l -> verb.in_l
body.out_r -> verb.in_r
verb.out_l -> out.left : 0.7
verb.out_r -> out.right : 0.7
```

The sympathetic strings are never struck. They only sound when the driven
string feeds them, at a tenth of its level, and they damp slower than it does
— so the room keeps ringing after the note stops. That lag is the sound.

Extend it by closing the loop: `symp1.out -> main.in : 0.012` returns a tenth
of the coupling and the whole set becomes one instrument. Keep the return an
order of magnitude below the send or the network howls.

## Variations

**Bowed.** Continuous noise through a VCA is a bow. No strike at all.

```text
n   = noise { color: pink, seed: 3 }
bow = vca { gain: 0 }
n -> bow.in
press = lfo { freq: 0.07hz, wave: sine }
0.22 -> bow.cv
press -> bow.cv : 0.12
body = modal { modes: 64, freq: A2, structure: 0.27, brightness: 0.45,
               damping: 0.8, position: 0.31 }
bow -> body.in
```

Bow pressure is `vca.cv`. Move it slowly (0.05–0.1 Hz) and the tone breathes.
Too much pressure just gets loud and noisy — the model has no stick-slip.

**Blown.** Same rig, `structure` near 0.22 (compressed, tube-ish), `position`
0.5 to null the even partials, and a slow `func` for the breath attack:

```text
env = func { rise: 0.35, fall: 1.2 }
clk -> env.trig
env -> breath.cv : 0.3
bore = modal { modes: 32, freq: A3, structure: 0.22, brightness: 0.55,
               damping: 0.55, position: 0.5 }
```

**Prepared / detuned.** Two strings on the same strike, one 15 cents off, plus
a third at high `structure` for the rattle of an object on the string:

```text
0.0013 -> b.v_oct                # ~2 cents: slow beat, one instrument
prep = string { seed: 4, damping: 0.6, structure: 0.94, position: 0.5, freq: A2 }
a.out -> prep.in : 0.15
```

**Structure morph.** The drum↔harmonic↔bell axis is an automation lane, not a
setting. Zero the base or the config value adds to your cable:

```text
body = modal { modes: 64, freq: A2, structure: 0, damping: 0.7,
               brightness: 0.45, position: 0.31 }
morph = stepseq { steps: [0.05, 0.25, 0.45, 0.8] }
sm = func { rise: 0.6, fall: 0.6 }
morph.cv -> sm.in
sm -> body.structure
```

Below 0.25 partials compress toward the fundamental — membranes, toms, thuds.
0.25–0.3 is exactly harmonic: a plucked pitch. Above that they stretch until,
at 1, you are in bar-and-bell territory with no fusing fundamental.

**Explicit ratios.** `ratios:` replaces `modes:` and places every partial by
hand. Two sets worth memorising:

```text
bar  = modal { ratios: [1, 2.756, 5.404, 8.933, 13.34, 18.64],
               freq: G3, damping: 0.62, brightness: 0.5, position: 0.28 }
bell = modal { ratios: [1, 2, 2.4, 3, 4, 5.33, 6.67, 8, 9.6],
               freq: A2, damping: 0.78, brightness: 0.35, position: 0.22 }
```

The first is the ideal free-free bar (Fletcher & Rossing): glockenspiel,
vibraphone, tubular bell. Enormous gaps between partials, so it reads as
pitched, glassy, and slightly hollow — nothing fills the octave above the
fundamental. The second is a tuned church bell, hum note first: the minor
third (2.4 = tierce) is what makes bells sound sad, and the ear hears the
*nominal* at 4 as the strike note, an octave and a bit above what you typed.
Set `freq` two octaves below where you want the bell to land.

The membrane set `[1, 1.594, 2.136, 2.296, 2.653, 2.918]` (Bessel zeros) is
the kick/tom/snare body in `patches/lib/kit/`; `[1, 2.66, 5.4]` is the rim.

With `ratios:` given, `structure` becomes a warp on your list —
`w = 2^((structure − 0.25) · 2)` — exact as written at the 0.25 default. It is
still an automation target: a slow sweep bends a fixed bell between metal and
membrane.

**Infinite drone.** `string` damping ≥ 0.95 crossfades toward a lossless loop;
only `damping: 1.0` is genuinely lossless. At 0.99 a struck A1 still loses
~0.8 dB/s — audible over a minute. Pick 1.0 for a drone that must not sag, and
gate it with a VCA because nothing else will ever stop it.

```text
dr = string { seed: 3, damping: 1.0, structure: 0.26, position: 0.3, freq: A1 }
```

`modal` has no such crossover: its Q tops out at 5×10⁶, long but finite. For
an eternal bed use `string`; for a very long bell use `modal { damping: 0.95 }`
and let it die.

**Odd/even stereo.** `out = odd + even`, so splitting them costs nothing and
gives a wide, phase-coherent field from a mono voice:

```text
body.odd  -> verb.in_l : 0.8
body.even -> verb.in_r : 0.8
```

Combine with `position`: as the comb nulls one group, the image leans to the
other side by itself.

## The knob that matters

`damping`. It is the envelope, the size of the object, and the genre all at
once. 0.4 is a muted pluck, 0.7 a guitar, 0.85 a piano with the pedal down,
0.95+ a drone. Every other parameter is a colour on top of that decision.

Second place: `position` on `modal`. It is a comb on the mode amplitudes.
Near 0 or 1 there is no comb and every partial sounds — the brightest, most
metallic hit. At 0.25 the null lands on the 3rd partial (and the 7th, 11th).
At 0.5 it nulls every even partial: hollow, clarinet-like, sounds an octave
higher than it is. The fundamental always survives, so you can sweep it live
without losing pitch.

## Modulating it

- `brightness` under a 0.03–0.05 Hz LFO — the single cheapest "this is alive"
  move. ±0.15 around 0.5.
- `position` under a slow tri: the comb walks through the partials and the
  timbre morphs without a filter anywhere.
- `structure` per section (see above) — drum in the verse, bell in the chorus,
  same module.
- Strike-position from velocity: `sh` the accent at the trigger, then
  `acc -> body.brightness : 0.3`. That is exactly what `kit/tom.sub` does, and
  it is why the kit responds to dynamics.
- Pitch bend on `v_oct` from a 0.3 ms/80 ms `func`: skin tension on a drum,
  a bent note on a string.

## Gotchas

- **A resonator with nothing exciting it is silent.** Check the render RMS,
  not the check output. `strike` needs a *rising edge*; a constant 1 fires
  once at t=0 and never again.
- **Config values are bases, cables sum onto them.** `structure: 0.25` plus a
  0..1 lane gives 0.25..1.25 (clamped). Zero the base when a cable owns it.
- Free-ringing voices ghost across arrangement sections. Gate the *strike*
  with the section lane (`logic { op: and }`), not just the output VCA.
- `damping: 1.0` on `string` really is forever, including through the section
  where you wanted silence.
- Note names only land in tune when `freq` is an A (`A1`/`A2`/`A3`). These
  modules default to 220 Hz, which is A3 — leave it, or set `freq: A2`, and
  drive pitch entirely from `v_oct`.
- `ratios:` and `modes:` are mutually exclusive; entries clamp to 64; a
  non-positive entry is a patch error.
- Coupling gains above ~0.25 turn a sympathetic network into feedback. The
  Rings number is 0.2/n.
- `string` bottoms out near 5.9 Hz at 48 kHz — subsonic strings are a real
  texture, not an error.

## Where it shows up

Kosmische and fourth-world ambient (`patches/rings.patch`,
`patches/lib/sym-drone.sub`), everything Rings-adjacent in modular, gamelan
and Reich-style pulse music via `ratios:`, and the whole physically-modelled
drumkit in `patches/lib/kit/`. `patches/permafrost.patch` uses sympathetic
strings as the harmonic bed the granular relay feeds on. `patches/afterglow`,
`umbra`, `selene`, and `motorik` all use one of these as their pitched body.

## Push it further

1. Feed a `modal` bank with another `modal` bank's output instead of noise —
   a bell exciting a drum is a sound no acoustic instrument makes.
2. Tune four sympathetic strings to a just chord (0.58496 / 0.32193 /
   0.80735) and strike none of them; play only the coupling gains from an
   arrangement lane. The chord fades in and out with no VCA in the path.
3. Put `follow` on a drum bus and patch `env -> string.in` — the kit plays the
   strings, the way a snare rattles when the room is loud.
4. Automate `ratios`-warp `structure` and `position` in opposite directions
   from the same LFO; the spectrum both stretches and re-combs, which reads as
   an object physically changing shape.
5. Run the bowed patch's excitation through `fold` first. Wavefolded bow
   pressure gives the resonator a harmonic series that grows with amplitude —
   West Coast physics on an East Coast body.
