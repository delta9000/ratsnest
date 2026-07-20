# Deep house — a 9th chord breathing against a kick

## The feel

Warm, unhurried, slightly sad, and never more than four things at once. The
groove is not in the drums — it is in the *pump*, the way the pad and bass duck
and refill around a kick you barely notice. Chords are always extended: a bare
triad in this genre sounds like a mistake, because the 7th and 9th are exactly
what keeps the harmony from resolving and the loop from ending.

Everything here is subtractive. Build eight elements, then take four away.

## Clock

118–124 BPM. Sixteenths for hats and shakers, eighths for everything musical.

```text
# ---- clock tree: sixteenths at 122 BPM ----
master   = clock { bpm: 488 }
eighth   = div { by: 2 }
quarter  = div { by: 2 }
half     = div { by: 2 }
bar      = div { by: 2 }
four_bar = div { by: 4 }
master -> eighth.in
eighth -> quarter.in
quarter -> half.in
half -> bar.in
bar -> four_bar.in
```

## Palette

| Element | Modules | Starting values |
|---|---|---|
| chord stack | 4× `vco { wave: saw/tri }` + `func` + `svf.lp` | cutoff 900, res 0.2, fall 0.55 |
| Rhodes | `fm { algo: dual }` | ratio 14 modulator, `curve: log`, long t2 |
| bass | `vco { wave: tri }` + `ladder` | cutoff 160, res 0.15, offbeat eighths |
| kick | `vco` sine + `func { curve: log }` | 48 Hz, fall 0.2, bend 1.6 — soft, not techno |
| duck | `func { rise: 0.004, fall: 0.26, curve: exp }` on `quarter` | the instrument |
| hats | `noise` → `svf.hp` on offbeat eighths + 16th shaker | 8 k / 6 k |
| glue | `compressor { sidechain: external }` | ratio 4, attack 1 ms, release 150 ms |

## Signature moves

### 1. Four-note voicings, and the fifth is the note you drop

The house vocabulary is m7, maj7, m9, 9sus4 — never triads. With four voices
you cannot fit 1-3-5-7-9, so drop the fifth: it carries no information the root
does not. Intervals above the root, in v/oct:

| Chord | Voicing | v/oct offsets |
|---|---|---|
| m7 | 1 b3 5 b7 | 0, 0.25, 0.58333, 0.83333 |
| maj7 | 1 3 5 7 | 0, 0.33333, 0.58333, 0.91667 |
| m9 | 1 b3 b7 9 | 0, 0.25, 0.83333, 1.16667 |
| maj9 | 1 3 7 9 | 0, 0.33333, 0.91667, 1.16667 |
| 9sus4 | 1 4 b7 9 | 0, 0.41667, 0.83333, 1.16667 |

Write the lanes as **absolute** note names, not offsets, and voice-lead them by
hand so each voice moves by a step or holds. Here Fm9 | Dbmaj9 | Bbm7 | Eb9sus4
— every chord puts its 9th on top, and the top voice walks G–Eb–C–F.

```text
# ---- chord lanes: Fm9 | Dbmaj9 | Bbm7 | Eb9sus4, one per bar ----
ch_root = stepseq { steps: [F2v,  Db2v, Bb1v, Eb2v] }
ch_v1   = stepseq { steps: [Ab3v, F3v,  Ab3v, Ab3v] }
ch_v2   = stepseq { steps: [Eb4v, C4v,  Db4v, Db4v] }
ch_v3   = stepseq { steps: [G4v,  Eb4v, C4v,  F4v] }
bar -> ch_root.clock
bar -> ch_v1.clock
bar -> ch_v2.clock
bar -> ch_v3.clock
```

Note names are absolute pitches around `A3v = 0.0`, so every oscillator reading
them needs an A-based `freq` base (220/110/440). See `docs/patch-craft.md`.

### 2. The chord is played off the beat and glides between changes

House chords land on the "and", not the downbeat. A `func` slew on each lane
smears the bar change so the chord *melts* into the next one instead of
switching — the single cheapest way to sound like a keyboard player.

```text
# ---- chord stack: syncopated stabs, glided lanes ----
gl_1 = func { rise: 0.05, fall: 0.05 }
gl_2 = func { rise: 0.05, fall: 0.05 }
gl_3 = func { rise: 0.05, fall: 0.05 }
ch_v1.cv -> gl_1.in
ch_v2.cv -> gl_2.in
ch_v3.cv -> gl_3.in

cha = vco { freq: 220hz, wave: saw }
chb = vco { freq: 220hz, wave: saw }
chc = vco { freq: 220hz, wave: tri }
gl_1 -> cha.v_oct
gl_2 -> chb.v_oct
gl_3 -> chc.v_oct
0.005 -> chb.v_oct
-0.004 -> chc.v_oct

ch_seq = stepseq { steps: [0,0,0,0, 0,0,0,0, 0,0,0,0, 0,0,0,0],
                   gates: [0,0,1,0, 0,1,0,0, 0,0,1,0, 0,1,0,1] }
master -> ch_seq.clock
ch_env = func { rise: 0.008, fall: 0.55, curve: exp }
ch_seq.gate -> ch_env.trig
ch_filt = svf { cutoff: 900, res: 0.2 }
cha -> ch_filt.in : 0.24
chb -> ch_filt.in : 0.22
chc -> ch_filt.in : 0.2
ch_env -> ch_filt.cutoff : 1400
ch_amp = vca { gain: 0, response: exp }
ch_filt.lp -> ch_amp.in
ch_env -> ch_amp.cv : 0.8
```

Keep the `gates:` and `steps:` arrays the same length — `stepseq` takes its
length from `steps:` alone and silently truncates a longer gate array.

### 3. The Rhodes is FM, and it is one note

Two operators, ratio 1 and ratio 14, `curve: log`, a long second segment. Play
it sparsely on the chord's 9th and let the reverb do the rest.

```text
# ---- Rhodes: fm on the top voice, one note per bar ----
ep = fm { algo: dual, ratios: [1, 1, 14, 1, 1, 1], levels: [.5, .3, .08, 0, 0, 0],
          t2: [.6, .09, .05, 0, 0, 0], l2: [.5, .2, .12, 0, 0, 0],
          l3: [.14, 0, 0, 0, 0, 0], curve: log }
half -> ep.gate
gl_3 -> ep.v_oct
```

### 4. The pump is three layers deep

Derive the duck from the *quarter clock*, not the kick audio: a clean shape you
can tune, and no bleed. Then apply it at channel, return, and bus.
`ratsnest-composition/references/mixing.md` has the full numbers; the house
values are attack ~1 ms, release 120–180 ms, ratio 4:1, 4–6 dB of reduction.

```text
# ---- kick + the three-layer duck ----
duck = func { rise: 0.004, fall: 0.26, curve: exp }
quarter -> duck.trig
kick_env = func { rise: 0.001, fall: 0.2, curve: log }
quarter -> kick_env.trig
kick_osc = vco { freq: 48hz, wave: sine }
quarter -> kick_osc.sync
kick_env -> kick_osc.v_oct : 1.6
kick_amp = vca { gain: 0 }
kick_osc -> kick_amp.in
kick_env -> kick_amp.cv : 0.9
kick_drive = drive { amount: 0.18, mode: soft }
kick_amp -> kick_drive.in

beat_not = logic { op: not }
quarter -> beat_not.a
offbeat = logic { op: and }
eighth -> offbeat.a
beat_not -> offbeat.b
bass_env = func { rise: 0.002, fall: 0.15, curve: exp }
offbeat -> bass_env.trig
bass_osc = vco { freq: 110hz, wave: tri }
ch_root.cv -> bass_osc.v_oct
bass_filt = ladder { cutoff: 160, res: 0.15, drive: 0.4 }
bass_osc -> bass_filt.in
bass_env -> bass_filt.cutoff : 700
bass_amp = vca { gain: 0 }
bass_filt -> bass_amp.in
bass_env -> bass_amp.cv : 0.85

# layer 1: per-channel
band = mix { levels: [0.82, 0.6, 0.55, 0.3], pans: [0, 0, 0.1, -0.1] }
kick_drive -> band.in1
bass_amp -> band.in2
ch_amp -> band.in3
ep -> band.in4 : 0.5
duck -> band.lvl2 : -0.26
duck -> band.lvl3 : -0.18
duck -> band.lvl4 : -0.14

# layer 2: the return ducks too, or the reverb fills the gap
space = reverb { decay: 0.72, damp: 0.35, mix: 1, predelay: 0.022 }
ch_amp -> space.in_l : 0.3
ch_amp -> space.in_r : 0.3
ep -> space.in_l : 0.25
ep -> space.in_r : 0.2
bus = mix { levels: [1, 1, 0.5, 0.5], pans: [-1, 1, -0.9, 0.9] }
band.out_l -> bus.in1
band.out_r -> bus.in2
space.out_l -> bus.in3
space.out_r -> bus.in4
duck -> bus.lvl3 : -0.16
duck -> bus.lvl4 : -0.16

# layer 3: bus glue, keyed off the kick audio
glue = compressor { sidechain: external, threshold: -16, ratio: 4, knee: 5,
                    attack: 0.001, release: 0.15, makeup: 1.5, mix: 0.9 }
bus.out_l -> glue.in_l
bus.out_r -> glue.in_r
kick_drive -> glue.side_l : 0.7
kick_drive -> glue.side_r : 0.7
glue.out_l -> out.left
glue.out_r -> out.right
```

If the arrangement drops the kick, gate `duck` with the kick's lane or the
breakdown pumps against nothing.

### 5. Subtract, don't add

Deep house arrangements work by removal. Get the full groove running first,
then author lanes that *take things away* — a bar with no hats, eight with no
bass, a break where only the Rhodes and the reverb survive.

## Arrangement

```text
# Arrangement (32 four-bar blocks, 7.87 s each at 122 BPM):
#   0-1   intro    hats + chord stack only, filtered down
#   2-3   pulse    kick in, bass still out
#   4-7   groove   bass enters; the loop is now complete
#   8-11  full     Rhodes comps on top
#   12-13 subtract hats out for eight bars — the only "event"
#   14-19 full2    everything back, filter opens 400 Hz
#   20-21 break    kick + bass out; chord + reverb alone
#   22-27 groove2  rebuild in reverse order of removal
#   28-30 fade
#   31    tail     silent guard
```

## Mix targets

- Drop RMS ≈ **−12 dBFS**, crest ~11 dB, but the *pump* should show as ±1.5 dB
  of RMS wobble inside a 4 s window — check with a short `--stats` window.
- Kick soft and low (48 Hz, `drive: 0.18`); the bass, not the kick, carries the
  weight. Bass mono below 150.
- Chords own 300 Hz–3 kHz, high-passed at 200. Rhodes sits inside that band —
  carve a narrow `eq` dip in the chords at the Rhodes' centre, not a shelf.
- Hats above 6 k, quiet, ducked −0.12 by the quarter so they breathe.
- Reverb return wide (±0.9) and ducked. Long predelay (20–25 ms) keeps the
  chord's attack dry.

## In this repo

- `patches/meridian.patch` — chord lanes feeding bass, pad, arp and stabs from
  three numbers; the post-fader `stab_out` VCA that keeps muted parts out of
  the sends.
- `patches/halcyon.patch` — the offbeat `eighth AND NOT quarter` bass and the
  three-lane harmony with real voice leading.
- `patches/keys.patch` / `patches/fm.patch` — FM electric-piano configs worth
  copying wholesale.
- `patches/groove.patch` — the minimal drum skeleton to build on.

## How to break it

1. **Put the chord on a `grains` freeze.** Send `ch_amp` to `grains` with
   `density: 6, size: 0.4, spread: 0.8` and freeze it on the bar. The chord
   becomes a cloud that survives its own mute.
2. **Swing the sixteenths.** Clock the hats from a `stepseq` gate lane rather
   than the divider, with an uneven `gates:` array — the machine stops being a
   machine.
3. **Duck off something other than the kick.** Feed `follow` from the Rhodes
   and duck the pad with it. The pad now breathes around the melody.
4. **Two chords, alternating on a 3-bar cycle** against a 4-bar drum loop. The
   harmony realigns every 12 bars and the loop stops sounding like a loop.
5. **Delete the drums entirely** for the first 90 seconds and let the duck
   envelope run anyway. The pulse is already in the pad.

## Listening

- **Larry Heard / Mr. Fingers — "Can You Feel It"** — the whole genre in one
  bassline and one pad. Count the elements: three.
- **Moodymann — "Shades of Jae"** — chords hitting late and dragging; that lag
  is a `func` rise, not a timing error.
- **Kerri Chandler — "Rain"** — 9sus4 voicings and a Rhodes with more reverb
  than signal.
- **Theo Parrish — "Falling Up"** — the pump as the only rhythmic event; the
  drums barely exist.
- **Motor City Drum Ensemble — "Raw Cuts #2"** — subtractive arrangement done
  in public: listen to what leaves and when.
- **Isolée — "Beau Mot Plage"** — a chord loop that never resolves because
  every voicing keeps its 9th.
