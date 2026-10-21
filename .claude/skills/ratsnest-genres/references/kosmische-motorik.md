# Kosmische / motorik — a road at night, and the road never turns

## The feel

A beat that does not vary, will not vary, and after ninety seconds stops
sounding like a beat and starts sounding like a surface you are travelling
over. Above it, something sustained that barely moves at all. The tension
between the two is the whole composition; the moment either one develops, the
spell breaks.

## Clock

120–140 BPM, straight 4/4, no swing, no fills, no variation for the length of
the piece. One clock at sixteenths, everything derived, so the drums, the
ostinato and the tape echo are phase-locked by construction.

```text
master   = clock { bpm: 520 }          # sixteenths at 130 BPM
eighth   = div { by: 2 }
quarter  = div { by: 2 }
bar      = div { by: 4 }
four_bar = div { by: 4 }
master -> eighth.in
eighth -> quarter.in
quarter -> bar.in
bar -> four_bar.in
```

## Palette

| Element | Modules | Starting values |
|---|---|---|
| kick | `vco { wave: sine }` + `func { fall: 0.2, curve: log }` | 52 Hz, pitch env depth 1.6 |
| snare (2 & 4) | `noise` → `svf.bp` → `vca` | 1900 Hz, `res: 0.45`, fall 0.14 |
| offbeat rim | `logic` (eighth AND NOT quarter) → `modal.strike` | `structure: 0.14`, `damping: 0.2` |
| pad (stasis) | three `vco { wave: saw }`, **no envelope, ever** | root + fifth + octave tri |
| pad motion | two `lfo` at 0.017 Hz and 0.0071 Hz → `svf.cutoff` | base 620, depths 260 / 400 |
| ostinato | `euclid { steps: 7 }` → `stepseq` → `string` | `damping: 0.7`, klon overdrive |
| tape echo | `delay { feedback: 0.3, slew: 0.35 }` on the **drum bus** | 173 ms, wow + flutter LFOs |
| glue | `overdrive { model: klon, mix: 0.6 }` | on the ostinato, not the mix |

## Signature moves

### 1. The Apache beat, built exactly and then left alone

Kick on every quarter, snare on 2 and 4, and a dry offbeat tick between them.
Dinger played it live with mechanical evenness — the machine quality comes
from *invariance*, not from quantisation, so the one thing you must not do is
program a fill. Derive the offbeat as "eighth AND NOT quarter" from the same
div chain and it can never drift.

```text
kick_env = func { rise: 0.0008, fall: 0.2, curve: log }
quarter -> kick_env.trig
kick_osc = vco { freq: 52hz, wave: sine }
quarter -> kick_osc.sync
kick_env -> kick_osc.v_oct : 1.6
kick_amp = vca { gain: 0 }
kick_osc -> kick_amp.in
kick_env -> kick_amp.cv : 0.95

snare_pat = stepseq { steps: [0,0,0,0], gates: [0,1,0,1] }
quarter -> snare_pat.clock
snare_env = func { rise: 0.001, fall: 0.42, curve: exp }
snare_pat.trig -> snare_env.trig
snare_n = noise { color: white, seed: 5 }
snare_bp = svf { cutoff: 1900, res: 0.45 }
snare_n -> snare_bp.in
snare_amp = vca { gain: 0 }
snare_bp.bp -> snare_amp.in
snare_env -> snare_amp.cv : 0.55

notq = logic { op: not }
quarter -> notq.a
offbeat = logic { op: and }
eighth -> offbeat.a
notq -> offbeat.b
rim = modal { modes: 12, freq: 440hz, structure: 0.14, brightness: 0.7,
              damping: 0.2, position: 0.4 }
offbeat -> rim.strike
rim_env = func { rise: 0.0005, fall: 0.15, curve: exp }
offbeat -> rim_env.trig
rim_amp = vca { gain: 0 }
rim.out -> rim_amp.in
rim_env -> rim_amp.cv : 0.3
```

Note `snare_pat` carries a `gates:` array and dummy `steps:` — its `cv` is
unused; the rhythm is entirely in the gates. `rim`'s pitch lives in its config
block, not on a cable: `freq` is an input with a 220 Hz base, so `440 ->
rim.freq` would sum to 660.

### 2. Stasis: a pad with no envelope and no chord change

Everything else in electronic music articulates. This does not. Three
oscillators, one chord, sounding continuously from the first sample to the
last, with two slow filter LFOs as the only motion. Give it a fifth and an
octave rather than a third — the mode stays open, so the ostinato above it can
imply major or minor as it likes.

```text
pad_r = vco { freq: E2, wave: saw }
pad_f = vco { freq: E2, wave: saw }
pad_o = vco { freq: E3, wave: tri }
0.58333 -> pad_f.v_oct
0.006 -> pad_o.v_oct
pad_filt = svf { cutoff: 620, res: 0.18 }
pad_r -> pad_filt.in : 0.32
pad_f -> pad_filt.in : 0.28
pad_o -> pad_filt.in : 0.2
breath = lfo { wave: sine, freq: 0.017hz }
tide   = lfo { wave: tri,  freq: 0.0071hz }
breath -> pad_filt.cutoff : 260
tide -> pad_filt.cutoff : 400
pad_amp = vca { gain: 0.5 }
pad_filt.lp -> pad_amp.in
```

The 59 s and 141 s LFO periods don't line up with each other or with any bar
count, so the pad's brightness never repeats where the drums do.

### 3. Echo the drums, not the lead

Conny Plank's move: the tape delay goes on the *rhythm section*, pushing it
back into a large space so an unvarying pattern reads as vast rather than
tiny. Keep feedback low (0.3) and add wow and flutter as two LFOs on the delay
time — one slow and deep, one fast and shallow.

```text
drum_bus = vca { gain: 0.9 }
kick_amp  -> drum_bus.in : 0.85
snare_amp -> drum_bus.in : 0.5
rim_amp   -> drum_bus.in : 0.4

tape = delay { time: 0.1731, feedback: 0.3, mix: 1, tone: 2400, slew: 0.35 }
drum_bus -> tape.in : 0.3
wow     = lfo { wave: sine, freq: 0.27hz }
flutter = lfo { wave: tri,  freq: 6.3hz }
wow -> tape.time : 0.0011
flutter -> tape.time : 0.00018
```

173.1 ms is an eighth at 130 BPM — just off the grid enough after the `slew`
smoothing to blur rather than double. Return it on its own channel, panned
opposite the rim.

### 4. Interlocking ostinato on a coprime cycle

The guitars in this music don't riff; they cycle at a length that doesn't
divide the bar, so the same figure lands in a different place every time
round. A 7-step `euclid` on eighths against 4/4 realigns every 7 bars, and
clocking a 5-step pitch sequence from the *pattern's* triggers stretches the
figure to 35 eighths — which comes home against the bar only every 35 bars.
Twelve numbers, and it never sounds like a loop.

```text
ost_pat = euclid { steps: 7, hits: 3 }
eighth -> ost_pat.clock
ost_seq = stepseq { steps: [E3v, B3v, G3v, A3v, B3v] }
ost_pat.trig -> ost_seq.clock
ost = string { seed: 3, damping: 0.7, position: 0.24, brightness: 0.55 }
ost_seq.cv -> ost.v_oct
ost_pat.trig -> ost.strike
ost_od = overdrive { model: klon, drive: 1.8, tone: 0.6, level: 0.5, mix: 0.6 }
ost.out -> ost_od.in : 0.5
ost_amp = vca { gain: 0.45 }
ost_od.out -> ost_amp.in
```

### 5. Accretion instead of arrangement

There is no drop and no breakdown. Elements accumulate, hold for a long time,
and subtract in the reverse order they arrived. A single lane with a long slew
does the entire job — 3 s rise means no entry is ever an event.

```text
arr = stepseq { steps: [0,.2,.45,.7, .85,1,1,1, 1,1,1,.8, .6,.35,.1,0] }
four_bar -> arr.clock
arr_lvl = func { rise: 3, fall: 4 }
arr.cv -> arr_lvl.in
arr_lvl -> ost_amp.cv : 0.5
arr_lvl -> drum_bus.gain : 0.1
```

## Arrangement

```text
# Arrangement (16 four-bar blocks, 7.38 s each at 130 BPM):
#   0     ignition   pad alone, filtered low
#   1-2   the beat   kick + rim enter at full level and never change
#   3-4   snare      2 & 4 arrives; tape send opens
#   5-7   cruise     ostinato enters and cycles; nothing else happens
#   8-10  plateau    everything at level; only the pad LFOs move
#   11-13 subtract   ostinato out, then snare, then rim
#   14-15 tail       pad and tape echo alone, fading
```

Blocks 8–10 are the test. If you feel the need to add something there, add
`0.02` of drive to the drum bus and nothing else. Longer is better: 16 blocks
is two minutes, and the genre's real form is seven to ten.

## Mix targets

| Slot | Owner | Note |
|---|---|---|
| 40–90 Hz | kick fundamental | mono; the pad is high-passed out of here |
| 90–300 Hz | pad body | the bed; keep it *loud*, this is not techno |
| 300–900 Hz | snare body, ostinato low end | the mud zone — carve the pad here |
| 1k–4k | rim, ostinato character, snare crack | dry and centred |
| 4k+ | tape echo hiss, snare air | wide, dark (`tone: 2400`) |

Drop-window RMS **−14 to −16 dB**, crest ~13 dB. The pad sits *higher* in the
mix than a techno pad would — it is co-lead, not glue. Duck it under the kick
only lightly (`kick_env -> bus.lvl2 : -0.12`); a heavy pump imposes a groove
on music whose point is that it has no groove. Pan the rim and the tape return
apart (±0.3) and leave kick, snare and pad centred.

## In this repo

- `patches/motorik.patch` — the direct ancestor of this card: 122 BPM,
  Em–D–C–Em, string drone into `phaser`, guitar via `string` + klon overdrive,
  and a `grains` freeze that locks the drone bed into a kosmische pad. Steal
  the freeze section and the guitar chain wholesale.
- `patches/permafrost.patch` — motorik's structure at 118 BPM with a
  two-cloud freeze relay; also the cleanest kick/hat/bass staging in the repo.
- `patches/lib/sym-drone.sub` — the sustained bed as one line.
- `patches/lib/kit/rim.sub` and `snare.sub` — physically modelled versions of
  move 1's noise-and-bandpass placeholders; contract is `trig` / `accent` /
  `tune` / `decay`.
- `patches/nocturne.patch` — the note-name idiom, if you want the ostinato
  spelled as pitches rather than floats.

## How to break it

1. **Change chord exactly once, seven minutes in.** One move to the VII and
   stay there. After that much invariance a single change is an event on the
   scale of a key change.
2. **Drop the kick and keep everything else.** The pattern survives in the rim
   and the echo; when the kick returns it sounds twice as heavy for having
   been absent, and you never had to build a riser.
3. **Let the tape echo run away.** Ride `tape.feedback` to 0.85 for four bars
   under the plateau. Plank did this with a fader; you do it with a lane.
4. **Make the ostinato 11 steps instead of 7.** It stops interlocking and
   starts *shifting* — the same figure now needs 11 bars to come home, which
   is past the point where the ear tracks it.
5. **Play the drums badly on purpose.** `bern { prob: 0.97 }` on the rim
   trigger drops one tick in thirty. Invariance with a flaw in it is a human
   at a kit, which is what this music actually was.

## Listening

- **Neu!, "Hallogallo"** — the beat, definitively. Ten minutes, one pattern,
  and the entire arrangement is guitar layers arriving and leaving.
- **Neu!, "Negativland"** — the same engine used for menace. Steal the
  treated-drum echo.
- **Kraftwerk, *Autobahn*** — motion as subject. Note how little the harmony
  does and how much the timbre does.
- **Harmonia, *Musik von Harmonia*** — organ stasis over the pulse; the
  clearest model for this card's move 2.
- **Cluster, *Zuckerzeit*** — the same aesthetic with drum machines and no
  live drummer. Proof the invariance, not the performance, carries it.
- **Can, *Tago Mago*** — Jaki Liebezeit's evenness with actual dynamics
  underneath. Listen for how the hi-hat, not the kick, holds the time.
- **La Düsseldorf, *Viva*** — motorik at anthem scale; the pad is loud, bright
  and unashamed.
- **Stereolab, *Emperor Tomato Ketchup*** — motorik with chord changes and
  vocals, showing exactly how much you can add before the spell breaks.
