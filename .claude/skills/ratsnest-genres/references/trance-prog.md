# Trance / progressive — 32 bars of withholding, then the pad comes back

## The feel

The sound design is not the genre; the *macro-form* is. A trance track is a
machine for delaying gratification: strip everything away, rebuild it one layer
at a time over sixteen bars, then spend eight bars lifting — filter opening,
riser rising, snare roll accelerating — before the pad returns at full width.
Get that arc right with three cheap voices and it works. Get it wrong with a
perfect supersaw and it doesn't.

Progressive is the same arc stretched: fewer elements, longer blocks, the
breakdown never fully strips.

## Clock

132–140 BPM. Sixteenths — the gate pattern needs them.

```text
# ---- clock tree: sixteenths at 138 BPM ----
master   = clock { bpm: 552 }
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
| supersaw | 7× `vco { wave: saw }` on one pitch | detune 0, ±5, ±12.5, ±25 cents |
| pad body | 2× `vco` saw on 3rd and 5th | −6 dB under the supersaw |
| air layer | `vco` saw → `svf.hp` 4 kHz + slow LFO | widest thing in the mix |
| gate | `stepseq { gates: [...] }` → `func { fall: 0.06 }` → `vca` | sixteenths |
| bass | `vco` saw + `ladder` on offbeat eighths | cutoff 200, res 0.2 |
| kick | `vco` sine + `func { curve: log }` + `drive` | 50 Hz, fall 0.17 |
| riser | `noise` → `svf.lp` + `func { rise: 8, fall: 0.4 }` | cutoff sweep 6 kHz |
| roll | `euclid` with `hits` on the riser lane + short `func` | 4 → 14 hits |

## Signature moves

### 1. Supersaw: seven saws, one pitch, a symmetric detune spread

Seven detuned saws on the **root**, and two plain saws for the third and fifth.
Do not build 21 oscillators — the shimmer comes from the unison stack, and the
chord only needs its other tones present, not thick. Offsets in v/oct, in
cents: 0, ±5, ±12.5, ±25. Widen the spread and the pad gets bigger and less
pitched; past ±35 cents it stops having a key.

Stage the outer voices quieter than the centre or the fundamental disappears.

```text
# ---- harmony: Am | F | C | G (i - VI - III - VII), one chord per bar ----
ch_root  = stepseq { steps: [A2v, F2v, C3v, G2v] }
ch_third = stepseq { steps: [C3v, A2v, E3v, B2v] }
ch_fifth = stepseq { steps: [E3v, C3v, G3v, D3v] }
bar -> ch_root.clock
bar -> ch_third.clock
bar -> ch_fifth.clock
pad_gl = func { rise: 0.06, fall: 0.06 }
ch_root.cv -> pad_gl.in

# ---- supersaw: 7 detuned saws on the root ----
ss1 = vco { freq: 220hz, wave: saw }
ss2 = vco { freq: 220hz, wave: saw }
ss3 = vco { freq: 220hz, wave: saw }
ss4 = vco { freq: 220hz, wave: saw }
ss5 = vco { freq: 220hz, wave: saw }
ss6 = vco { freq: 220hz, wave: saw }
ss7 = vco { freq: 220hz, wave: saw }
pad_gl -> ss1.v_oct
pad_gl -> ss2.v_oct
pad_gl -> ss3.v_oct
pad_gl -> ss4.v_oct
pad_gl -> ss5.v_oct
pad_gl -> ss6.v_oct
pad_gl -> ss7.v_oct
0.0042 -> ss2.v_oct
-0.0042 -> ss3.v_oct
0.0104 -> ss4.v_oct
-0.0104 -> ss5.v_oct
0.0208 -> ss6.v_oct
-0.0208 -> ss7.v_oct

pad_filt = svf { cutoff: 1400, res: 0.15 }
ss1 -> pad_filt.in : 0.17
ss2 -> pad_filt.in : 0.15
ss3 -> pad_filt.in : 0.15
ss4 -> pad_filt.in : 0.13
ss5 -> pad_filt.in : 0.13
ss6 -> pad_filt.in : 0.1
ss7 -> pad_filt.in : 0.1
```

### 2. Three frequency zones, not three saws in the same octave

A lush pad is a sub anchor, a mid chord body, and an air layer high-passed
above 4 kHz with its own slow movement. Three saws fighting in one octave is
mud; three saws in three zones is "huge".

```text
# ---- pad body (3rd + 5th) and the air layer ----
body_gl3 = func { rise: 0.06, fall: 0.06 }
body_gl5 = func { rise: 0.06, fall: 0.06 }
ch_third.cv -> body_gl3.in
ch_fifth.cv -> body_gl5.in
body_a = vco { freq: 220hz, wave: saw }
body_b = vco { freq: 220hz, wave: saw }
body_gl3 -> body_a.v_oct
body_gl5 -> body_b.v_oct
0.006 -> body_b.v_oct
body_a -> pad_filt.in : 0.11
body_b -> pad_filt.in : 0.11

air_osc = vco { freq: 440hz, wave: saw }
pad_gl -> air_osc.v_oct
1 -> air_osc.v_oct                   # an octave above the stack
air_hp = svf { cutoff: 4200, res: 0.3 }
air_osc -> air_hp.in
air_drift = lfo { wave: sine, freq: 0.037hz }
air_drift -> air_hp.cutoff : 1600
air_amp = vca { gain: 0.18 }
air_hp.hp -> air_amp.in
```

### 3. The trance gate is a sixteenth `stepseq` gate lane

An LFO into a VCA gives you tremolo. A **gate lane** gives you a rhythm: author
the sixteenths by hand, drive a short `func` from `.gate` (which respects the
array) rather than `.trig` (which fires every step), and put that on a VCA
after the pad's filter. Uneven arrays are what make it groove — `[1,0,1,1,
0,1,0,1, 1,0,1,0, 1,1,0,1]` is a pattern; all-ones is a buzz.

The `func`'s `fall` is the whole character: 0.03 s is a stutter, 0.09 s is a
breath, 0.2 s smears the gate away entirely. Automate it from the arrangement
and the pad opens up into the drop on its own.

```text
# ---- trance gate ----
gate_seq = stepseq { steps: [0,0,0,0, 0,0,0,0, 0,0,0,0, 0,0,0,0],
                     gates: [1,0,1,1, 0,1,0,1, 1,0,1,0, 1,1,0,1] }
master -> gate_seq.clock
gate_env = func { rise: 0.002, fall: 0.06, curve: exp }
gate_seq.gate -> gate_env.trig
pad_chop = vca { gain: 0 }
pad_filt.lp -> pad_chop.in
gate_env -> pad_chop.cv : 0.9
pad_out = mix { levels: [0.7, 0.5], pans: [0, 0] }
pad_chop -> pad_out.in1
air_amp -> pad_out.in2
```

### 4. The 32-bar breakdown: strip, rebuild, lift

Eight four-bar blocks. Blocks 0–1 strip (drums gone, pad exposed, filter down).
Blocks 2–5 rebuild, adding exactly one element per block. Blocks 6–7 lift:
filter opens, riser attacks slowly, roll density climbs. Author it as lanes and
the section conducts itself.

The riser's asymmetry is the trick — `rise: 8, fall: 0.4`. It takes two blocks
to arrive and one beat to vanish, so the drop lands on silence.

```text
# ---- breakdown lanes (values are the 8 blocks of the 32-bar section) ----
arr_drums = stepseq { steps: [0,0, 0,0,0,1, 1,1] }   # gates a logic: keep it 0/1
arr_pad   = stepseq { steps: [1,1, .9,.8,.7,.7, .8,1] }
arr_gate  = stepseq { steps: [0,.2, .4,.6,.7,.8, .9,1] }
arr_rise  = stepseq { steps: [0,0, 0,0,0,0, .5,1] }
four_bar -> arr_drums.clock
four_bar -> arr_pad.clock
four_bar -> arr_gate.clock
four_bar -> arr_rise.clock
pad_lvl  = func { rise: 4, fall: 4 }
gate_lvl = func { rise: 3, fall: 3 }
rise_cv  = func { rise: 8, fall: 0.4 }
arr_pad.cv -> pad_lvl.in
arr_gate.cv -> gate_lvl.in
arr_rise.cv -> rise_cv.in
gate_lvl -> pad_filt.cutoff : 2600
gate_lvl -> gate_env.fall : -0.03      # the gate tightens as it rises

# ---- lift: riser + accelerating roll ----
riser_n = noise { color: white, seed: 7 }
riser_f = svf { cutoff: 300, res: 0.55 }
riser_n -> riser_f.in
rise_cv -> riser_f.cutoff : 6000
riser_amp = vca { gain: 0 }
riser_f.lp -> riser_amp.in
rise_cv -> riser_amp.cv : 0.6

roll_pat = euclid { steps: 16, hits: 4, rot: 0 }
master -> roll_pat.clock
rise_cv -> roll_pat.hits : 10
roll_env = func { rise: 0.001, fall: 0.05, curve: exp }
roll_pat.trig -> roll_env.trig
roll_n = noise { color: white, seed: 41 }
roll_f = svf { cutoff: 2600, res: 0.45 }
roll_n -> roll_f.in
roll_amp = vca { gain: 0 }
roll_f.bp -> roll_amp.in
roll_env -> roll_amp.cv : 0.26
rise_cv -> roll_amp.cv : 0.16
```

### 5. Rolling offbeat bass, kick on the quarter, nothing else

The drop is not busy. Kick, offbeat bass eighths on the chord root, hats, gated
pad. Every extra element costs you the width you spent 32 bars earning.

```text
# ---- kick + rolling bass ----
kick_gate = logic { op: and }
quarter -> kick_gate.a
arr_drums.cv -> kick_gate.b
kick_env = func { rise: 0.0006, fall: 0.17, curve: log }
kick_gate -> kick_env.trig
kick_osc = vco { freq: 50hz, wave: sine }
kick_gate -> kick_osc.sync
kick_env -> kick_osc.v_oct : 1.8
kick_amp = vca { gain: 0 }
kick_osc -> kick_amp.in
kick_env -> kick_amp.cv : 0.95
kick_drive = drive { amount: 0.3, mode: soft }
kick_amp -> kick_drive.in

beat_not = logic { op: not }
quarter -> beat_not.a
offbeat = logic { op: and }
eighth -> offbeat.a
beat_not -> offbeat.b
bass_env = func { rise: 0.0015, fall: 0.13, curve: exp }
offbeat -> bass_env.trig
bass_osc = vco { freq: 110hz, wave: saw }   # 110, not 220, *on purpose*: the
ch_root.cv -> bass_osc.v_oct                # bass sits an octave under the
                                            # chord root it shares a lane with
bass_filt = ladder { cutoff: 200, res: 0.2, drive: 0.5 }
bass_osc -> bass_filt.in
bass_env -> bass_filt.cutoff : 900
bass_amp = vca { gain: 0 }
bass_filt -> bass_amp.in
bass_env -> bass_amp.cv : 0.9

band = mix { levels: [0.85, 0.6, 0.65, 0.4],
             pans: [0, 0, 0, 0] }
kick_drive -> band.in1
bass_amp -> band.in2
pad_out.out_l -> band.in3
riser_amp -> band.in4 : 0.6
roll_amp -> band.in4 : 0.5
pad_lvl -> band.lvl3 : 0.2
kick_env -> band.lvl2 : -0.24
kick_env -> band.lvl3 : -0.18
band.out_l -> out.left
band.out_r -> out.right
```

## Arrangement

```text
# Arrangement (32 four-bar blocks, 6.96 s each at 138 BPM):
#   0-1   intro     kick + hats, filtered
#   2-5   groove    bass + gated pad enter
#   6     lift-1    riser + roll
#   7-14  DROP 1    full, gate at 0.9, everything narrow
#   15-16 strip     drums out, pad exposed, filter down    <- breakdown starts
#   17-20 rebuild   one element per block, gate lane climbing
#   21-22 lift-2    riser 8 s attack, roll 4 -> 14 hits
#   23-29 DROP 2    drop 1 + lead; two 16-bar phrases, variation at 26
#   30    fade
#   31    tail      silent guard
```

Cap the drop at 32–64 bars and split it into two 16-bar phrases with a clear
change at the boundary. See `ratsnest-composition/references/form.md` for the
grid-wrap arithmetic — guard blocks are mandatory.

## Mix targets

- Drop RMS ≈ **−12 dBFS**, crest ~11 dB. Breakdown 6–8 dB below that; intro
  −25 dB or lower. Those RMS steps are how you verify the arc without ears.
- **Narrow = loud.** Drop `stereo.width` to ~0.9 in the drop and open it to 1.4
  in the breakdown. The contrast is the whole illusion.
- Pad high-passed at 200 Hz; the air layer at 4.2 kHz owns everything above.
  Bass mono below 150.
- The gated pad and the kick must not overlap: the gate's `fall` should end
  before the next kick, or set the gate lane to skip the downbeat sixteenth.
- Duck at three depths (`ratsnest-composition/references/mixing.md`). The
  breakdown has no kick — gate the duck envelope with the drum lane.

## In this repo

- `patches/ion.patch` — trance-gate pad, accent-driven acid, supersaw hook,
  riser + roll into two drops. The closest existing thing to this card.
- `patches/halcyon.patch` — the chord-tracking `switch` arp and a three-detuned
  saw lead; also the full mastering chain with calibrated numbers.
- `patches/polaris.patch` / `patches/selene.patch` — the same arrangement DNA at
  ethereal tempos; steal the lane shapes, not the voices.

## How to break it

1. **Never resolve.** Use i – v – VI – VII and stop the progression one chord
   early at the drop. The tension the breakdown built stays unspent.
2. **Gate the kick, not the pad.** Put the sixteenth gate lane on the drum bus
   and leave the pad sustaining. Everything inverts and it still works.
3. **Make the riser tonal.** Replace the noise with the supersaw an octave up,
   `rise_cv -> osc.v_oct : 0.5`. A pitch riser is more unsettling than a
   noise sweep and nobody uses them any more.
4. **Skip the drop.** Do the full 32-bar lift and then bring back only the
   kick and the bass for eight bars. Deliver on bar 9.
5. **Detune the supersaw to ±45 cents** and drop the third and fifth entirely.
   The pad loses its key and becomes a texture — progressive, not uplifting.

## Listening

- **Robert Miles — "Children"** — the arc with almost no sound design. Time the
  breakdown; it is the template.
- **Above & Beyond — "Sun & Moon"** — the transgate pad. Listen for the gate
  *tightening* as the section rises.
- **Paul van Dyk — "For an Angel"** — one riff, all form; the lift is pure
  filter and roll.
- **Sasha & Digweed — *Northern Exposure*** — progressive's longer blocks and
  partial breakdowns; nothing ever fully strips.
- **Gouryella — "Gouryella"** — the supersaw at its widest; note how thin the
  arrangement is underneath it.
- **Solarstone — "Seven Cities"** — pad in three frequency zones, textbook.
