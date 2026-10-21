# Synthwave / Outrun — a snare in a room that closes before the tail finishes

## The feel

Slow, wide, and cinematic, built from three or four parts that each do one
obvious thing. The snare is enormous and cut short. The bass never rests — a
sixteenth-note arpeggio that outlines the chord instead of stating a root.
The lead is a pulse wave breathing through PWM, sitting alone in the mids
with a long plate behind it. Everything is major-key nostalgic or minor-key
noir, and there is no ambiguity anywhere; this genre has no interest in
subtlety.

## Clock

80–118 BPM, straight 4/4, backbeat on 2 and 4. Slower than it feels — the
sixteenth bass does the work of tempo.

```text
master  = clock { bpm: 400 }     # 100 BPM sixteenths
eighth  = div { by: 2 }
quarter = div { by: 2 }
bar     = div { by: 4 }
master -> eighth.in
eighth -> quarter.in
quarter -> bar.in
```

## Palette

| Element | Modules | Starting values |
|---|---|---|
| gated snare | `lib/kit/snare.sub` + `reverb` + `func` + `logic{cmp}` + `vca` | `decay: 0.72`, gate window ~180 ms |
| kick | `lib/kit/kick.sub` | `tune: 48hz`, straight quarters |
| bass arp | `switch { mode: seq, n: 8 }` + `vco{saw}` + `ladder` | cutoff 320 Hz base, env +900 |
| lead | `vco { wave: pulse }` + `lfo -> pwm` | `pwm : 0.34` at 0.45 Hz, vib 5.2 Hz |
| pad | 3–5 detuned `vco{saw}` + `ladder` + `adsr` | attack 0.6–1.0 s |
| space | `reverb` | `decay: 0.72, damp: 0.15, mix: 1` (wet-only return) |

Chords: minor with 7ths, i–VI–III–VII or i–VII–VI–VII. Three bar-clocked
`stepseq` lanes carrying root/third/fifth as absolute v/oct note names —
see `ratsnest-composition/references/harmony.md`.

## Signature moves

### 1. The gated reverb snare

This is the genre. A bright plate on the snare, then a gate that stays fully
open for a fixed window and slams shut — not a fade. Build the gate from a
decaying `func` compared against a threshold: while the envelope is above the
threshold the comparator outputs 1, and the instant it crosses, 0. The
`fall` time and the threshold together set the window length.

```text
snare  = sub { file: "lib/kit/snare.sub", decay: 0.3 }
sn_pat = euclid { steps: 4, hits: 2, rot: 1 }        # 2 and 4
quarter -> sn_pat.clock
sn_pat.trig -> snare.trig

plate = reverb { decay: 0.72, damp: 0.15, mix: 1 }   # wet-only
snare -> plate.in_l : 0.8
snare -> plate.in_r : 0.8

gate_env = func { rise: 0.001, fall: 0.34, curve: lin }
sn_pat.trig -> gate_env.trig
gate_cmp = logic { op: cmp }                          # a > b
gate_env -> gate_cmp.a
0.42 -> gate_cmp.b                                    # window ≈ 0.58 * 0.34 s
gl = vca { gain: 0 }
gr = vca { gain: 0 }
plate.out_l -> gl.in
plate.out_r -> gr.in
gate_cmp -> gl.cv
gate_cmp -> gr.cv
```

Raise `gate_cmp.b` for a shorter, tighter gate; lower it for the full
*In the Air Tonight* blast. The alternative detector is `follow { mode: peak,
thresh: 0.18 }` reading the snare and driving the same VCAs — its close point
tracks the hit's loudness instead of the clock, which is more natural and less
iconic. The iconic one is the fixed window.

Pan `gl`/`gr` hard-ish (±0.6) and keep the dry snare centred.

### 2. Sixteenth bass arp through a sequential switch

The switch steps a fixed 8-note *contour* through chord lanes, so the arp
re-voices itself over every chord without you writing a note.

```text
root  = stepseq { steps: [A1v, F1v, C2v, G1v] }
third = stepseq { steps: [C2v, A1v, Ds2v, As1v] }
fifth = stepseq { steps: [E2v, C2v, G2v, D2v] }
bar -> root.clock
bar -> third.clock
bar -> fifth.clock

arp = switch { mode: seq, n: 8 }
master -> arp.clock
root  -> arp.in1
fifth -> arp.in2
third -> arp.in3
fifth -> arp.in4
root  -> arp.in5
third -> arp.in6
fifth -> arp.in7
root  -> arp.in8

bosc  = vco { wave: saw, freq: 220hz }   # 220 base: `arp` carries `…v` note
arp -> bosc.v_oct                        # names, so 55 would put A1 at 14 Hz
benv  = func { rise: 0.003, fall: 0.3, curve: exp }
master -> benv.trig
bfilt = ladder { cutoff: 320, res: 0.2 }
bosc -> bfilt.in
benv -> bfilt.cutoff : 900         # base is the floor; the env sweeps up
bvca  = vca { gain: 0 }
bfilt -> bvca.in
benv  -> bvca.cv
```

Note names are Hz-domain absolutes with `A3v = 0.0`; `bosc`'s `freq: 55hz`
base is an A, so they land where written. Change that base to anything else
and the whole track transposes (`docs/patch-craft.md`).

### 3. PWM lead

A slow LFO on `pwm` gives the chorus-like shimmer without a chorus. A second,
faster LFO on `v_oct` at tiny depth is the vibrato. Pulse at `pw != 0.5`
carries DC that lowpass filters pass straight through — block it.

```text
lead = vco { wave: pulse, freq: 220hz, pw: 0.5 }
pwm  = lfo { freq: 0.45hz, wave: tri }
pwm -> lead.pwm : 0.34
vib  = lfo { freq: 5.2hz }
vib -> lead.v_oct : 0.004                 # ~7 cents
lead_dc  = dcblock { cutoff: 20 }
lead -> lead_dc.in_l
lead_env = adsr { attack: 0.02, decay: 0.3, sustain: 0.7, release: 0.4 }
bar -> lead_env.gate
lead_vca = vca { gain: 0 }
lead_dc.out_l -> lead_vca.in
lead_env -> lead_vca.cv
```

For the fatter "Juno" version, run two of these a few cents apart into
`chorus { voices: 3, rate: 0.4hz, depth: 0.6, mix: 0.5 }`.

### 4. Everything breathes with the kick

Synthwave's pump is gentle — this is not house. One layer of channel ducking
at −0.2 on bass and pad, plus the FX returns, and stop.

```text
kick_env -> m.lvl4 : -0.2        # bass
kick_env -> m.lvl5 : -0.15       # pad
kick_env -> m.lvl2 : -0.12       # gated-verb return
```

### 5. Toms and a sweep for the transitions

A `lib/kit/tom.sub` descending fill at the end of an 8-bar block, and a
`noise -> svf.bp` riser with `func { rise: 3.5 }` on the cutoff. Do not use a
crash; use the gated snare hit alone, dry, at the block boundary.

## Arrangement

```text
# Arrangement (24 four-bar blocks at 100 BPM, 9.6 s each):
#   0-1   intro    pad + arp only, filter closed (cutoff base 180)
#   2-3   build    kick enters on quarters; arp opens
#   4-9   verse    gated snare on 2 and 4; lead states the melody
#   10-11 lift     drums out, pad blooms, tom fill into
#   12-19 chorus   everything, lead an octave up, plate wetter
#   20-21 solo     lead alone over the arp; snare gate window shortens
#   22-23 outro    parts drop one per block; pad last, fading
```

Blocks are 4 bars and phrases are 8 — the form is deliberately obvious.

## Mix targets

- Slots: kick 30–100 Hz, bass arp 80–250 Hz (high-passed at 60), pad
  high-passed at 200 Hz, lead 700 Hz–3 kHz alone, gated verb 500 Hz+ and wide.
- Bass arp is mono. The pad and the gated snare return carry the width
  (`stereo { width: 1.5 }` on the pad bus only).
- Drop-window RMS ≈ −12 to −13 dBFS, crest ~11–13 dB. Warm, not crushed —
  `overdrive { model: klon, mix: 0.5 }` on the bus, then the standard chain
  (`ratsnest-composition/references/mastering.md`).
- The gated snare return is loud: level ~0.42 per side against a 0.55 dry
  snare. If it is not startling it is not the effect.

## In this repo

- `patches/minor-machinery.patch` — the arrangement-lane grid, chord lanes,
  and switch-driven contour, fully worked. Steal the lane table wholesale.
- `patches/halcyon.patch`, `patches/ion.patch` — supersaw stacks and the
  chord-lane discipline; `ion` is the "big, dumb, correct" pentatonic hook.
- `patches/lib/kit/snare.sub` — `snap` param trades wires against body; 0.7
  for a cracking gated snare, 0.35 for a Linn-ish thud.

## How to break it

1. **Gate the pad, not the snare.** Same `func`+`cmp` window on a wide
   supersaw at eighth notes and you have a trance gate in an outrun track.
2. **Make the arp contour move.** `lfo { freq: 0.02hz } -> arp.addr : 3`
   drifts the pattern's read position over minutes while `mode: seq` still
   advances — the riff reshapes itself without a new lane.
3. **Give the lead a portamento.** `func` with no `trig`, lane CV into
   `func.in`, output into `v_oct` — the slew rates become glide time, and
   the lead stops sounding like a step sequencer.
4. **Drop the kick for the whole chorus.** The sixteenth arp already implies
   the pulse; the gated snare marks the backbeat. Half of the best Carpenter
   cues have no kick at all.
5. **Detune the pad against the arp by 8 cents and leave it.** Not chorus —
   a permanent, slightly wrong beating that makes the whole track feel
   analogue.

## Listening

- John Carpenter, *Assault on Precinct 13* — three parts, no drums, arpeggio
  as tempo. The minimum viable version of this card.
- Tangerine Dream, *Love on a Real Train* — the sixteenth arp as harmony;
  everything else is a pad and a delay.
- Phil Collins, *In the Air Tonight* — the gated reverb reference. Note the
  gate closes *before* the tail is musically finished; that abruptness is
  the effect.
- Kavinsky, *Nightcall* — a saturated bass arp low in the mix, vocal in the
  centre, huge snare. Study the frequency separation, not the sounds.
- Com Truise, *Colorvision* — PWM leads plus deliberately sloppy timing;
  borrow the humanization from `references/idm.md` and apply it here.
- Perturbator, *Sentient* — the same palette pushed into distortion; the
  bass arp goes through an `overdrive { model: rat }`.
- Vangelis, *Blade Runner Blues* — the ancestor of the noir voicing: sustained
  minor chords with a lead that never resolves.
- Miami Nights 1984, *Accelerated* — the major-key, unashamed version. Chord
  lanes, four bars, no modulation at all.
