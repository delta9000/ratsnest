# Dub techno — one chord, hit once, dying for eight bars

## The feel

Berlin, 1993, a broken tape echo. Four or five voices, never more. The dry
chord is almost inaudible; what you hear is its sixth repeat coming back
through a plate with the top gone. Nothing develops — the decay *is* the
development. Economy of means is not a style choice here; it is the only way
the tail stays audible. Reaching for a seventh element? Delete two instead.

## Clock

115–130 BPM. No sixteenths needed — the fastest thing in the patch is a hat,
and the delay does the subdivision for you. Clock eighths.

```text
# ---- clock tree: eighths at 122 BPM ----
master  = clock { bpm: 244 }
quarter = div { by: 2 }
half    = div { by: 2 }
bar     = div { by: 2 }
two_bar = div { by: 2 }
master -> quarter.in
quarter -> half.in
half -> bar.in
bar -> two_bar.in
```

## Palette

| Element | Modules | Starting values |
|---|---|---|
| chord stab | 3× `vco { wave: saw }` + `func` + `vca` + `ladder` | m7 voicing, fall 0.3 s, cutoff 240, res 0.45 |
| saturation | `drive { mode: soft }` | amount 0.28 — **before** the send |
| echo | 2× `delay` + 2× `tempo` | dotted 8th & dotted quarter, fb .68/.55, tone 1900/1250 |
| plate | `reverb` | decay .86, damp .5, mix 1, predelay 38 ms, bandwidth .82 |
| tail filter | 2× `svf.lp` | 900 Hz, drifting ±320 on a 53 s LFO |
| sub | `vco { wave: sine }` + `follow` off the tail | 55 Hz, rms follower |
| kick | `vco` sine + `func { curve: log }` + `drive` | 45 Hz, fall 0.22, bend 1.45 |
| hiss | `noise { color: pink }` + `svf.hp` | into the delay input at 0.06 |
| rim/hat | `noise` + `svf.hp` + `euclid` | 5 hits of 16, rot 3 |

## Signature moves

### 1. The stab is a chord you never actually hear

Root, minor 3rd, minor 7th, once per bar, soft attack, fall long enough to be
a note rather than a click. Keep the dry level under everything. The `ladder`
base is the *floor* of the sweep; the envelope supplies the whole opening.

```text
# ---- stab: Dm7, once per bar, deliberately soft ----
st_a = vco { freq: 220hz, wave: saw }
st_b = vco { freq: 220hz, wave: saw }
st_c = vco { freq: 220hz, wave: saw }
D3v -> st_a.v_oct
F3v -> st_b.v_oct
C4v -> st_c.v_oct
0.004 -> st_b.v_oct              # ~7 cents of drift, not a chorus
-0.003 -> st_c.v_oct

stab_env = func { rise: 0.004, fall: 0.3, curve: exp }
bar -> stab_env.trig
stab_amp = vca { gain: 0, response: exp }
st_a -> stab_amp.in : 0.3
st_b -> stab_amp.in : 0.26
st_c -> stab_amp.in : 0.24
stab_env -> stab_amp.cv : 0.85
stab_filt = ladder { cutoff: 240, res: 0.45, drive: 0.7 }
stab_amp -> stab_filt.in
stab_env -> stab_filt.cutoff : 1700
```

### 2. Saturate before the send, then filter the repeats

Harmonics you do not generate before the delay cannot be sculpted after it.
`drive` first; then the echo's `tone` eats the top a little more every pass.
That progressive darkening *is* the tape emulation. Route pink noise into the
delay input rather than the mix and the hiss acquires the same rhythm and the
same decay — noise floor as an instrument, not a flaw.

```text
# ---- saturation and hiss, both pre-send ----
warm = drive { amount: 0.28, mode: soft }
stab_filt -> warm.in

hiss = noise { color: pink, seed: 23 }
hiss_hp = svf { cutoff: 1400, res: 0.1 }
hiss -> hiss_hp.in
```

### 3. Dotted-eighth time from `tempo` — and zero the base

The skank. `delay`'s `time` base is 0.4 s and cables **sum**, so the config
must be `time: 0ms` or the dotted eighth becomes 0.775 s of nothing in
particular. Two taps at different beat values interleave into a polyrhythm the
sequencer never played — dotted eighth against dotted quarter lands a repeat on
nearly every sixteenth without a single extra note.

```text
# ---- echo: dotted 8th + dotted quarter, filtered feedback ----
sync_de = tempo { bpm: 122, beats: 0.75 }
sync_dq = tempo { bpm: 122, beats: 1.5 }
echo_a = delay { time: 0ms, feedback: 0.68, tone: 1900, mix: 1, slew: 2.5 }
echo_b = delay { time: 0ms, feedback: 0.55, tone: 1250, mix: 1, slew: 3.5 }
sync_de.seconds -> echo_a.time
sync_dq.seconds -> echo_b.time
warm -> echo_a.in : 0.8
warm -> echo_b.in : 0.55
hiss_hp.hp -> echo_a.in : 0.06

echo_drift = lfo { wave: sine, freq: 0.017hz }
echo_drift -> echo_a.feedback : 0.055
echo_drift -> echo_b.feedback : -0.04
```

High `slew` (2.5–3.5 s) is deliberate here and *wrong* everywhere else: it
means a modulated delay time smears instead of lurching. Keep `slew` near zero
only when you want a flanger.

### 4. The cycle: echo → plate → LPF → sub → silence

The whole genre in five arrows. Delays feed the plate; the plate is low-passed
so the tail loses its top over its length; a follower on that low-passed tail
drives a sine sub, so the sub swells *with* the decay instead of sitting under
it. By the time the tail dies it is pure low frequency — and the next stab
lands on top of it.

```text
# ---- plate, tail filter, sub reinforcement ----
space = reverb { decay: 0.86, damp: 0.5, mix: 1, predelay: 0.038,
                 bandwidth: 0.82, mod_rate: 0.37, mod_depth: 42 }
echo_a -> space.in_l : 0.7
echo_a -> space.in_r : 0.32
echo_b -> space.in_l : 0.3
echo_b -> space.in_r : 0.7
warm -> space.in_l : 0.14
warm -> space.in_r : 0.14

tail_l = svf { cutoff: 900, res: 0.12 }
tail_r = svf { cutoff: 900, res: 0.12 }
space.out_l -> tail_l.in
space.out_r -> tail_r.in
tail_drift = lfo { wave: sine, freq: 0.019hz }
tail_drift -> tail_l.cutoff : 320
tail_drift -> tail_r.cutoff : 280

sub_track = follow { mode: rms, attack: 0.08, release: 0.4 }
tail_l.lp -> sub_track.in
sub_osc = vco { freq: 220hz, wave: sine }
D2v -> sub_osc.v_oct              # 73.4 Hz. The base MUST be 220 --
                                  # note names are relative to A3 = 220 Hz
sub_amp = vca { gain: 0 }
sub_osc -> sub_amp.in
sub_track.env -> sub_amp.cv : 4.0   # env off a low-passed tail peaks near
                                    # 0.12, not 1.0 -- see mixing.md
```

### 5. Drums you could remove without ruining the track

Kick and one high tick. No snare. The kick's job is to duck the wet field, not
to drive the room.

```text
# ---- kick + a single hat, ducking the returns ----
kick_env = func { rise: 0.001, fall: 0.22, curve: log }
quarter -> kick_env.trig
kick_osc = vco { freq: 45hz, wave: sine }
quarter -> kick_osc.sync
kick_env -> kick_osc.v_oct : 1.45
kick_amp = vca { gain: 0 }
kick_osc -> kick_amp.in
kick_env -> kick_amp.cv : 0.9
kick_drive = drive { amount: 0.2, mode: soft }
kick_amp -> kick_drive.in

mixer = mix { levels: [0.8, 0.16, 0.55, 0.62, 0.62],
              pans: [0, -0.12, 0, -0.55, 0.55] }
kick_drive -> mixer.in1
warm -> mixer.in2
sub_amp -> mixer.in3
tail_l.lp -> mixer.in4
tail_r.lp -> mixer.in5
kick_env -> mixer.lvl3 : -0.14
kick_env -> mixer.lvl4 : -0.19
kick_env -> mixer.lvl5 : -0.19
mixer.out_l -> out.left
mixer.out_r -> out.right
```

For the tick, take `dub-techno.patch`'s hat wholesale: `euclid { steps: 16,
hits: 5, rot: 3 }` on the eighth clock, holed by `bern { prob: 0.78 }`, into a
0.038 s `func` on high-passed noise, ducked −0.12 by the quarter.

## Arrangement

Dub techno usually wants no arrangement grid at all — see the infinite-scene
recipe in `ratsnest-composition/references/form.md`. If you do want blocks:

```text
# Arrangement (24 four-bar blocks, 7.87 s each at 122 BPM):
#   0-2   fog      echo returns only, no kick, stab every two bars
#   3-5   pulse    kick enters, hat sparse
#   6-11  groove   stab every bar, sub tracking, feedback at .68
#   12-13 dropout  kick out for eight bars; the tail carries alone
#   14-19 groove2  kick back, echo feedback +0.08, tail filter opens
#   20-22 fade     stab stops; render long enough to catch the decay
#   23    tail     silent guard
```

The dropout at 12–13 is the only "event" the genre allows. It works because
the echo does not stop.

## Mix targets

- Drop-window RMS ≈ **−14 dBFS**, crest ~13 dB. Space is the point; do not
  chase −12 (`ratsnest-composition/references/mixing.md`).
- Sub owns 30–100 Hz, mono, and is often the loudest single element. The stab
  lives 250–1200 Hz and never goes above it.
- High-pass the hat above 6 k and the dry stab around 200 Hz. Everything
  between belongs to the reverb tail.
- Returns at ±0.55, ducked −0.19 by the kick; dry stab near centre at 0.16 —
  a number that should look wrong and sound right. No brickwalling:
  `clipper { mode: soft, drive: 2, mix: 0.25 }` into `limiter { threshold: -0.8 }`.

## In this repo

- `patches/dub-techno.patch` — the reference implementation. Steal its two
  irrational LFOs on cutoff/resonance (19 s and 43 s), its Bernoulli hat holes,
  and the `turing → quant` harmonic drift in a narrow D-dorian window.
- `patches/permafrost.patch` — freeze-relay chord overlap, the same "the tail
  of the last chord is the pad of the next one" idea taken further.
- `patches/meridian.patch` — the `tempo`-owned delay times and the post-fader
  `stab_out` VCA that keeps muted parts out of the sends.

## How to break it

1. **Put the delay before the filter.** Feed raw saws into the echo and put the
   `ladder` on the *return*, sweeping slowly. Now the repeats change timbre
   together instead of independently — wronger, and much more physical.
2. **Push feedback past 1 on one tap.** `delay` self-oscillates and DC-blocks
   its own feedback. Ride it with a lane that pulls back to 0.6 after four
   bars; the near-runaway howl is a lead instrument.
3. **Make the stab a `modal` strike.** `modal { ratios: [...], damping: 0.9 }`
   into the same chain gives you a struck metal chord instead of a synth one.
   Rhythm & Sound with a gamelan.
4. **Let the sub follower run open.** Remove the kick entirely and let
   `sub_track` drive the sub off the reverb tail alone. The track now has a
   pulse with no percussion in it.
5. **Detune the two echo taps to non-integer beat values** — `beats: 0.7` and
   `beats: 1.43`. The repeats drift off the grid and reconverge every ~30 bars.

## Listening

- **Basic Channel — "Phylyps Trak"** — the delay is louder than the source;
  copy the level relationship, not the chord.
- **Basic Channel — "Quadrant Dub I"** — the filtered-feedback darkening curve;
  time how many repeats it takes to lose the top and set `tone` to match.
- **Maurizio — "M4.5"** — one stab, no melody at all. Proof that harmony is
  optional if decay is interesting.
- **Rhythm & Sound — "Never Tell You"** — hiss and hum as a mix element sitting
  above the drums. Route noise into the send, not the master.
- **Porter Ricks — *Biokinetics*** — the sub is the melody. Study how the low
  end moves while nothing else does.
- **Deepchord presents Echospace — *The Coldest Season*** — field recordings
  folded into the reverb tails; `grains` at high `jitter` does the same job.
- **Vladislav Delay — *Multila*** — the grid dissolving. Long `slew` on delay
  time is the ratsnest handle for this.
