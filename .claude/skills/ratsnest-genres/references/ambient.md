# Ambient — a room you can walk out of and come back to

## The feel

A bed that was already sounding before you arrived, with things happening in
it at intervals you can't predict but never resent. Nothing announces itself,
nothing resolves, and the loudest event is quieter than the quietest thing on
the radio. You should be able to stop listening for two minutes and lose
nothing.

## Clock

**No pulse.** Delete the div tree; a listener who can count your patch is
listening to something else. Time comes from four free-running cycles whose
periods share no small common multiple, plus one event clock so slow it reads
as weather rather than tempo.

```text
d1 = lfo { wave: sine, freq: 0.0189hz }   #  53 s
d2 = lfo { wave: tri,  freq: 0.0114hz }   #  88 s
d3 = lfo { wave: sine, freq: 0.0071hz }   # 141 s
d4 = lfo { wave: tri,  freq: 0.0033hz }   # 303 s

ev   = clock { bpm: 26 }                  # ~2.3 s between chances
rare = div { by: 7 }                      # ~16 s between events
ev -> rare.in
```

53/88/141/303 realign after about six days. One LFO per destination — two into
the same input is just a slower LFO. See `ratsnest-generative` for the full
five-primitive treatment; this card is the ambient dialect of it.

## Palette

| Element | Modules | Starting values |
|---|---|---|
| drone bed | two `vco { wave: tri }` + sine sub → `svf.lp` | 110 Hz pair, `0.0033` detune, cutoff 700 |
| coupled drone | `sub { file: "lib/sym-drone.sub" }` | `damp: 0.93`, `fifth: 0.58496` |
| events | `bern` → `modal.strike` / `string.strike` / `lpg.strike` | `prob: 0.38`, `damping: 0.88` |
| pitch guardrail | `sh` → `quant` | scale `[0,2,5,7,9]` or `[0,1,5,7,10]` |
| texture | `grains` with `size` 0.4–1.6 s, `density` 4–10 | `jitter: 0.9`, `spread: 0.85` |
| held moment | `grains.freeze` / `reverb.freeze` | gated by a comparator on a glacial LFO |
| space | `reverb { decay: 0.88, damp: 0.34, mix: 1 }` | its own mixer channel, wet-only |
| motion | four `lfo` at 0.003–0.02 Hz | one destination each |

Voices that ring after their trigger (`string`, `modal`, `lpg`, frozen
`grains`) are the whole point here — but they ghost when a lane fades them in
mid-tail. See `docs/patch-craft.md`, "free-ringing voices ghost".

## Signature moves

### 1. Beat two oscillators, then make the beat itself wander

The difference frequency between two near-identical pitches *is* the tempo of
the piece. Radigue chose it by ear at fractions of a Hz; you choose it as a
number. A fixed beat becomes a machine after two minutes, so put a glacial LFO
on the detune at a tenth of its depth: the pulse breathes, speeds, stalls.

```text
bed_a = vco { freq: 110hz, wave: tri }
bed_b = vco { freq: 110hz, wave: tri }
bed_c = vco { freq: 55hz,  wave: sine }
0.0033 -> bed_b.v_oct           # ~0.25 Hz beat: a four-second pulse
d1 -> bed_b.v_oct : 0.0018      # the beat rate itself wanders

bed_lp = svf { cutoff: 700, res: 0.12 }
bed_a -> bed_lp.in : 0.4
bed_b -> bed_lp.in : 0.4
bed_c -> bed_lp.in : 0.25
d2 -> bed_lp.cutoff : 320
```

Stack octaves and fifths, never thirds — an open bed lets a later event imply
the mode instead of fixing it.

### 2. Events, not more drift

A patch of pure drift is a wash; the ear stops reporting it. What makes it a
piece is discrete things happening against the bed — struck and decaying, so
they read as *events* rather than as another layer. Thin a slow clock with a
coin, hold the pitch through the tail, and keep the scale to five notes.

```text
coin = bern { prob: 0.38, seed: 9 }
rare -> coin.gate

wander = random { rate: 0.21hz, smooth: 1, seed: 3 }
hold = sh { mode: sample }
wander -> hold.in
rare -> hold.trig
scale = quant { scale: [0, 2, 5, 7, 9] }
hold -> scale.in : 0.5

bell = modal { modes: 48, structure: 0.62, brightness: 0.42, damping: 0.88 }
scale.out -> bell.v_oct
coin.a -> bell.strike
```

`coin.b` is free: give it a second voice an octave down and you have
call-and-response that never mechanically alternates.

### 3. Freeze is a compositional gesture, not a texture

`grains.freeze` halts recording while the grains keep reading: a moment
captured and held. Gate it from a metronome and it's an effect; gate it from a
comparator on your slowest LFO and it's a section — irregular holds lasting a
minute or more, releasing back into live sampling without a seam.

```text
cloud = grains { secs: 6, seed: 11 }
bed_lp.lp -> cloud.in : 0.6
frz = logic { op: cmp }
d4 -> frz.a
0.35 -> frz.b                   # frozen for ~35% of a 303 s cycle
frz -> cloud.freeze
0.5 -> cloud.position
d3 -> cloud.position : 0.35
0.6 -> cloud.size
6 -> cloud.density
0.85 -> cloud.spread
```

Two instances fed the same input hold identical buffers, so alternating their
freeze gates gives a relay where one pad always sounds — `permafrost.patch`
runs exactly that.

### 4. Recirculate the shimmer

Shimmer is a pitch shift *inside* the reverb's feedback path, not after it.
`grains { pitch: 1 }` with small grains is the shifter; feed the reverb's
output through it and back in at a third gain, and successive passes climb an
octave each time while decaying. Keep the return below ~0.4 or it blooms into
a howl ten minutes after your render window ends.

```text
shim = grains { secs: 2, seed: 21 }
1 -> shim.pitch                 # octave up
0.14 -> shim.size
28 -> shim.density
0.9 -> shim.spread

space = reverb { decay: 0.88, damp: 0.34, mix: 1 }
bell.out -> space.in_l : 0.5
bell.out -> space.in_r : 0.5
space.out_l -> shim.in : 0.5
shim.out_l -> space.in_l : 0.35
shim.out_r -> space.in_r : 0.35
d4 -> space.decay : 0.08
```

### 5. Compose the range, not the value

Every destination gets a base that is the darkest, quietest, dullest version
you would accept, and a cable depth that says how far it may travel. You are
not choosing a cutoff; you are choosing a window and letting a slow process
pick a point inside it. This is why `svf { cutoff: 700 }` plus `d2 : 320` is a
composition and `cutoff: 1020` is a setting.

## Arrangement

There is no grid. The architecture is **drift plus event density**, and the
only thing that moves over ten minutes is how often things happen and how much
of the bed you can hear.

```text
# Drift architecture (no bars, no blocks):
#   bed        always on, cutoff and detune on d1/d2
#   events     rate = rare (16 s) x coin prob; put d3 on coin.prob
#   freeze     comparator on d4: ~1-2 holds per five minutes
#   space      reverb.decay on d4, delay feedback on d3
#   density    one lfo -> bern.prob is your whole "arrangement"
```

Render 8–12 minutes. Read `--stats` at 15 s windows: you want RMS moving ±3 dB
on no discernible schedule and no two windows identical. Do the fade **in
post** — a fade lane in the patch fights the ring-out.

```bash
ffmpeg -i take.wav -af "afade=t=in:st=0:d=8,afade=t=out:st=560:d=25" out.wav
```

## Mix targets

| Slot | Owner | Note |
|---|---|---|
| 30–60 Hz | sine sub of the bed, alone | mono, no second element ever |
| 60–250 Hz | drone fundamentals | this is the whole body; don't hollow it |
| 250–700 Hz | the mud zone | cut here first when it fogs |
| 700–3k | events (bell, pluck, string) | the only place anything is allowed to be loud |
| 3k+ | shimmer, grain hash, reverb tail | widest, `spread: 0.85`+ |

Drop-window RMS **−20 to −26 dB, crest 18 dB or more**. Do not put a limiter
on this for loudness; a `dcblock` and a `limiter { threshold: -1 }` as a safety
net is the entire master chain. Ambient mastered to club loudness stops being
ambient (`ratsnest-composition/references/mixing.md` has the table).

## In this repo

- `patches/cathedral.patch` — the cleanest event-plus-freeze skeleton here:
  Karplus plucks into a huge plate whose `freeze` gate is just `div { by: 2 }`.
  Steal the freeze wiring and the 50 BPM "clock as weather" idea.
- `patches/permafrost.patch` — the two-cloud freeze relay. Steal the whole
  relay: `frz_a`/`frz_b` opposed, `rel_a`/`rel_b` crossfading, so a frozen pad
  never stops sounding while the harmony moves under it.
- `patches/lib/sym-drone.sub` — coupled root+fifth strings, `root`/`fifth`
  inputs, long tails. A one-line drone bed.
- `patches/lib/frozen-cloud.sub` — freeze packaged with the tuning fixes
  (tight scatter, newest-edge position, pitch 0) and a `hall` send.
- `patches/granular.patch` — minimal cloud-from-a-drone; read it before
  building your own `grains` staging.
- `patches/west-coast.patch` — `fold` → `lpg` strike, the softest event voice
  in the repo. Steal the wander → `sh` → `quant` melody chain.
- `patches/fathom.patch` — infinite scene done properly: 53/88/141/303 s LFOs
  and no arrangement grid at all.

## How to break it

1. **Put one pulse in it.** A single kick every eight seconds, at −24 dB under
   everything, turns drift into anticipation. Deepchord's entire career is
   ambient with one pulse left in.
2. **Let one event be far too loud.** One bell at 0.9 into the plate, once
   every three minutes. The dynamic range you preserved is only worth
   something if you spend it.
3. **Introduce a dissonance and never resolve it.** A minor second held in the
   bed (`0.08333 -> bed_c.v_oct`) is unbearable for ten seconds and beautiful
   at minute six.
4. **Make the scale one note.** Delete the quantizer, drone the root, and let
   `structure`/`brightness`/`position` on the `modal` carry all the variation.
   Pitch is not the only parameter that can be melodic.
5. **Record something real.** `sampler` a field recording into `grains` at
   `size: 1.6, density: 4, pitch: -1` until its source is unrecognisable, and
   let it be the bed instead of the oscillators.

## Listening

- **Brian Eno, *Ambient 1: Music for Airports*** — loop lengths of 20.1 s and
  19.6 s against each other. Steal the incommensurability, not the piano.
- **Steve Roach, *Structures From Silence*** — one 30-minute take, shaded live
  on the console. The arrangement is EQ and reverb-send moves; nothing enters.
- **Éliane Radigue, *Trilogie de la Mort*** — beating as the only event
  source. Detune measured in fractions of a Hz over 20-minute spans.
- **Sarah Davachi, *Pale Bloom*** — organ and synth treated as the same
  instrument; hold a fifth and let the overtones do the composing.
- **Stars of the Lid, *And Their Refinement of the Decline*** — bowed strings
  arriving from under the noise floor. Steal the attack times: 8–15 seconds.
- **Hiroshi Yoshimura, *Music for Nine Post Cards*** — sparse struck events
  over near-silence. Proof that four notes and a room is a record.
- **Kaitlyn Aurelia Smith, *EARS*** — LPG strikes and wavefolds as the event
  vocabulary; timbre moves more than pitch does.
- **Alessandro Cortini, *Volume Massimo*** — one drone, one envelope, an LPG,
  and a lot of saturation. The minimal patch, played rather than programmed.
