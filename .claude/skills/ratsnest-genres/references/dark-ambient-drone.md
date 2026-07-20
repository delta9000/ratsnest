# Dark ambient & drone — pressure, not volume

## The feel

Something large is in the room with you and it is not moving. The low end is
felt in the chest before it is heard as pitch, and it has grit in it — rust,
wind, a bearing that needs oil. Nothing brightens; when it gets louder it is
because a layer that was always there stopped being hidden.

## Clock

**No pulse, and no implied one.** If you want a heartbeat, put it at 40–60 BPM
and never let it articulate a downbeat. Structure comes from one very slow
cycle that gates a very long envelope — minutes of rise, tens of seconds of
fall.

```text
pulse = lfo { wave: square, freq: 0.0028hz }   # one edge every ~6 min
arc = func { rise: 180, fall: 60, curve: exp }
pulse -> arc.trig
```

`func` on a square LFO is your only "arrangement": a 3-minute exponential
swell and a 1-minute collapse. Route it to filter cutoffs and send levels, not
to a master fader.

## Palette

| Element | Modules | Starting values |
|---|---|---|
| sub | `vco { wave: sine }` at 30–45 Hz | one, mono, never two |
| grain on the sub | `noise { color: pink }` → `svf.bp` at 90 Hz | `res: 0.55`, mixed at 0.4 |
| the summing grit | `drive { amount: 0.22, mode: soft }` | saturate the *sum*, not the sine |
| metallic howl | `delay { time: 9ms, feedback: 0.93, slew: 0.002 }` | a comb, not an echo |
| body | `modal { structure: 0.1, damping: 0.95 }` bowed by noise | `noise -> vca -> m.in` |
| wind / weather | `noise { color: pink }` → `svf.bp`, `random` on cutoff | `rate: 0.09hz` |
| smear | `grains { size: 1.6, density: 4, pitch: -0.5 }` | `jitter: 0.9` |
| space | `reverb { decay: 0.9, damp: 0.55 }`, dark | damp high: no sparkle |

## Signature moves

### 1. Rumble is low frequency *plus grain*

A 32 Hz sine is not a rumble; it is a test tone. Rumble is what happens when
low frequency has texture riding on it — resonance, noise, and a little
distortion so the harmonics tell your ears there is something down there even
on a phone speaker. Saturate the *sum* of the sine and the noise band, not the
sine alone, so the drive intermodulates them.

```text
sub_osc = vco { freq: 32.7hz, wave: sine }
sub_amp = vca { gain: 0.55 }
sub_osc -> sub_amp.in

grit = noise { color: pink, seed: 4 }
grit_bp = svf { cutoff: 90, res: 0.55 }
grit -> grit_bp.in
gr_amp = vca { gain: 0.35 }
grit_bp.bp -> gr_amp.in

rumble = drive { amount: 0.22, mode: soft }
sub_amp -> rumble.in
gr_amp -> rumble.in : 0.4
```

### 2. A delay short enough to be a resonator

Push a delay to 5–15 ms with feedback near 0.95 and it stops being an echo: it
is a comb filter ringing at `1/time`, i.e. a pitched metallic howl you excite
with noise instead of playing. Sweep the time on a glacial LFO and the howl
glides through the harmonic series. Keep `slew` near zero or the time smoother
smears the sweep into mush.

```text
howl = delay { time: 0.0091, feedback: 0.93, mix: 1, tone: 2200, slew: 0.002 }
grit -> howl.in : 0.06
sweep = lfo { wave: tri, freq: 0.0077hz }
sweep -> howl.time : 0.0035
howl_amp = vca { gain: 0.18 }
howl -> howl_amp.in
```

9.1 ms rings at ~110 Hz. Tune it against your sub deliberately: an octave
locks and thickens, a tritone snarls.

### 3. Crescendo by unmasking, not by adding

The layer was there the whole time under a closed filter. Open it and the
piece grows without a single new voice entering — which is why the growth
feels inevitable rather than arranged. This is the Berlin School's
filter-opening arc stretched to a timescale with no beat to measure it
against.

```text
veil = svf { cutoff: 110, res: 0.3 }
gr_amp -> veil.in
arc -> veil.cutoff : 5200
veil_amp = vca { gain: 0 }
veil.hp -> veil_amp.in
arc -> veil_amp.cv : 0.5
```

Run three of these on three buried layers with `arc` depths of 5200, 900 and
2400 and they unmask at different rates from one envelope.

### 4. One harmonic move, held for the whole piece

Phrygian menace is a single gesture: root, then flat second, and nothing else
ever. Slew the step so it arrives over twelve seconds instead of jumping —
half the effect is not being able to say when it happened.

```text
slowclk = clock { bpm: 3 }
turn = div { by: 8 }
slowclk -> turn.in
step = stepseq { steps: [0, 0, 0, 0.08333] }
turn -> step.clock
gl = func { rise: 12, fall: 12 }
step.cv -> gl.in
gl -> sub_osc.v_oct
```

### 5. Fake the field recording

You have no microphone, so build weather out of filtered noise with a random
walk on the band, then destroy its identity in `grains` at long grain sizes
and low density. Pitched down half an octave it reads as *place* rather than
as synthesis.

```text
wind = noise { color: pink, seed: 31 }
gust = random { rate: 0.09hz, smooth: 1, seed: 12 }
wind_bp = svf { cutoff: 420, res: 0.72 }
wind -> wind_bp.in
gust -> wind_bp.cutoff : 300

smear = grains { secs: 8, seed: 3 }
wind_bp.bp -> smear.in : 0.7
1.6 -> smear.size
4 -> smear.density
0.9 -> smear.jitter
0.8 -> smear.spread
-0.5 -> smear.pitch
```

## Arrangement

No blocks. The architecture is **buried layers plus one unmasking envelope**.

```text
# Unmasking architecture (one 12-minute breath):
#   layer 0  sub + grit          always audible, the floor
#   layer 1  comb howl           veiled at 110 Hz, opens over 180 s
#   layer 2  bowed modal body    veiled at 300 Hz, opens over 240 s
#   layer 3  wind smear          always audible, level on d3
#   the arc  func rise 180 / fall 60, retriggered every ~6 min
#   the move one phrygian step, slewed over 12 s, once
```

Render 10–14 minutes. In `--stats`, the tell for a working unmask is RMS that
climbs 6–8 dB over minutes while **peak barely moves** — you are revealing
sustained energy, not adding transients. If peak climbs with RMS you added a
layer instead of unveiling one.

## Mix targets

| Slot | Owner | Rule |
|---|---|---|
| 20–60 Hz | the sub, alone | mono; `eq { hp_freq: 22 }` and nothing below |
| 60–200 Hz | grit band, comb howl fundamental | the identity of the rumble |
| 200–700 Hz | bowed body | high-pass everything else out of here |
| 700–4k | metallic partials, wind | keep dull; `reverb { damp: 0.55 }`+ |
| 4k+ | grain hash only | if it sparkles, you made ambient instead |

RMS **−22 to −26 dB**, crest 18 dB+. `dcblock` is mandatory (`drive` and
`fold` both push DC). A limiter at −1 dBFS as a seatbelt; no clipper, no glue
compressor — a compressor is exactly the thing that will flatten your slow
crescendo back into a plateau.

## In this repo

- `patches/dark-ambient.patch` — misnamed (it's a 124 BPM chord piece), but
  steal its **four phase-offset triangle LFOs crossfading four fixed
  oscillator banks**: a progression with no sequencer and no note events, and
  the smoothest chord-change mechanism in the repo.
- `patches/cathedral.patch` — `reverb.freeze` held for whole bars while dry
  material plays against the frozen wall. The wall is your drone.
- `patches/fathom.patch` — glacial-LFO discipline: what to put each of the
  four periods on so nothing moves in step.
- `patches/shadow-lattice.patch` — read the header for how to keep generative
  material *dark and locked* rather than busy.
- `patches/lib/sym-drone.sub` — set `damp: 0.95` and it crosses over to
  infinite sustain; the coupled second string is your first buried layer.

## How to break it

1. **Put a heartbeat in it.** A 48 BPM sub thud at −26 dB, felt not heard.
   Dark ambient with a pulse is where Deepchord and Lustmord actually live.
2. **Let something be beautiful.** One major third, once, in the middle of the
   piece, unresolved after. The dread only works if it had an alternative.
3. **Go from near-silence to violence in four seconds.** Everything on this
   card says "slow"; the one violation the genre rewards is a cut, not a fade.
4. **Add a voice-like formant.** Two `svf.bp` at 700 Hz and 1150 Hz on the
   noise bed is an "ah". Nothing frightens like something that almost speaks.
5. **Make the low end silent for a minute.** Remove the floor and the mids
   become enormous by contrast; drop it back in and it hits like a new
   element.

## Listening

- **Lustmord, *The Place Where the Black Stars Hang*** — the sub is the
  composition. Steal the discipline of one low element at a time.
- **Thomas Köner, *Permafrost*** — pitchless low-frequency mass; almost all
  content between 30 and 200 Hz, moving by filtering only.
- **Deathprod, *Morals and Dogma*** — feedback and resonant howl as melodic
  material; the "audio virus" is a comb pushed to self-oscillation.
- **Sunn O))), *Black One*** — amplifier distortion as texture. Steal the
  saturate-the-sum trick; the grit is what makes low frequencies audible.
- **Kali Malone, *The Sacrificial Code*** — just-intonation drones with
  beating suppressed rather than exploited. Use `0.58496`, not `0.58333`.
- **Tim Hecker, *Ravedeath, 1972*** — degraded organ smeared into grain.
  Steal the long grain sizes and the willingness to distort a pad.
- **Éliane Radigue, *Adnos*** — three hours built from beating alone; proof
  that "nothing happens" is a technique, not a failure.
- **Biosphere, *Substrata*** — field recording placed *in front of* the drone
  rather than under it. The one move that makes a drone a place.
