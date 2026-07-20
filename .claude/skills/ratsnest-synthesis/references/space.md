# Space — delay and reverb are instruments, not sweeteners

## When to reach for it

When the decay is supposed to be the composition: dub techno, ambient,
Berlin School counterpoint, shoegaze, anything where the dry signal is
deliberately quiet. Also when a part is correct but lifeless — that's usually
a missing pre-delay send, not a missing note.

The rule that organizes everything here: **put the space on a tempo grid, then
make the grid drift.** A delay locked to a dotted eighth is rhythm; the same
delay with a 0.03 Hz LFO on `tone` and `feedback` is weather.

## Core patch

The dub chain: stab → saturate → tempo-synced filtered delays → plate →
low-pass, with the kick ducking every return.

```text
master = clock { bpm: 480 }         # sixteenths at 120
eighth = div { by: 2 }
quarter = div { by: 2 }
bar = div { by: 4 }
master -> eighth.in
eighth -> quarter.in
quarter -> bar.in

# --- the source: one short minor chord stab per bar ---
ch = stepseq { steps: [A2v, F2v, D2v, E2v] }
bar -> ch.clock
o1 = vco { wave: saw, freq: 220hz }
o2 = vco { wave: saw, freq: 220hz }
o3 = vco { wave: tri, freq: 220hz }
ch.cv -> o1.v_oct
ch.cv -> o2.v_oct
ch.cv -> o3.v_oct
0.25 -> o2.v_oct                    # minor third
0.58333 -> o3.v_oct                 # fifth
senv = func { rise: 0.006, fall: 0.3, curve: exp }
bar -> senv.trig
samp = vca { gain: 0, response: exp }
o1 -> samp.in : 0.34
o2 -> samp.in : 0.28
o3 -> samp.in : 0.3
senv -> samp.cv
sfilt = ladder { cutoff: 260hz, res: 0.42, drive: 0.6 }
samp -> sfilt.in
senv -> sfilt.cutoff : 1500

# --- saturation BEFORE the delay: harmonics for the filter to eat later ---
warm = drive { amount: 0.24, mode: soft }
sfilt -> warm.in

# --- tempo-synced echoes. ZERO the time base first: cables SUM onto it. ---
de = tempo { bpm: 120, beats: 0.75 }    # dotted eighth = 375 ms
tri8 = tempo { bpm: 120, beats: 1.5 }   # 3/8 = 750 ms
echo_a = delay { time: 0ms, feedback: 0.68, tone: 2000, mix: 1, slew: 2.5 }
echo_b = delay { time: 0ms, feedback: 0.56, tone: 1300, mix: 1, slew: 3.5 }
de.seconds -> echo_a.time
tri8.seconds -> echo_b.time
warm -> echo_a.in : 0.8
warm -> echo_b.in : 0.55
# Tape character: the feedback tone drifts, so each pass is darker than the last.
tone_drift = lfo { freq: 0.031hz, wave: tri }
tone_drift -> echo_a.tone : 420
tone_drift -> echo_b.tone : 260

# --- plate, then a low-pass on the whole wet field ---
plate = reverb { decay: 0.86, damp: 0.45, mix: 1,
                 predelay: 0.035, bandwidth: 0.84, mod_rate: 0.4, mod_depth: 40 }
echo_a -> plate.in_l : 0.7
echo_a -> plate.in_r : 0.3
echo_b -> plate.in_l : 0.3
echo_b -> plate.in_r : 0.7
warm -> plate.in_l : 0.15
warm -> plate.in_r : 0.15
tail_l = svf { cutoff: 1.6khz, res: 0.08 }
tail_r = svf { cutoff: 1.6khz, res: 0.08 }
plate.out_l -> tail_l.in
plate.out_r -> tail_r.in

# --- kick, so the returns have something to duck against ---
kenv = func { rise: 0.001, fall: 0.24, curve: log }
quarter -> kenv.trig
kosc = vco { wave: sine, freq: 46hz }
quarter -> kosc.sync
kenv -> kosc.v_oct : 1.5
kamp = vca { gain: 0 }
kosc -> kamp.in
kenv -> kamp.cv : 0.9

m = mix { levels: [0.85, 0.18, 0.34, 0.6, 0.6],
          pans: [0, 0, 0.3, -0.45, 0.45] }
kamp -> m.in1
warm -> m.in2                       # the dry stab is the quietest thing here
echo_a -> m.in3
tail_l.lp -> m.in4
tail_r.lp -> m.in5
kenv -> m.lvl3 : -0.14
kenv -> m.lvl4 : -0.2
kenv -> m.lvl5 : -0.2
m.out_l -> out.left
m.out_r -> out.right
```

Order matters and is not negotiable: saturation **before** the delay (so the
filter has harmonics to sculpt), the low-pass **after** the reverb (so each
generation is darker than the last), the ducking on the **returns**, not just
the source.

## Variations

**Zeroing the time base — the classic self-own.** Cables sum onto input
bases, and `delay { time: }` defaults to 0.4 s. This:

```text
sync = tempo { bpm: 120, beats: 0.75 }
dly = delay { feedback: 0.5, mix: 1 }
sync.seconds -> dly.time            # WRONG: 0.4 + 0.375 = 0.775 s
```

is a 0.775 s delay that is in sync with nothing. Always
`delay { time: 0ms, ... }` when a `tempo` owns the input.

**The dotted-8th / 3-8 lattice.** `beats: 0.75` (dotted eighth) against
`beats: 1.5` (three eighths) is the dub ricochet: the two lines land together
every three beats and are maximally apart in between. Pan them opposite.
Other useful pairs: `0.75` vs `0.5` (mainstage's arp lattice), `0.25` vs
`0.375` for a nervous stutter.

**Feedback near self-oscillation.** `feedback: 0.68` with `tone: 2000` is
about as far as you go before the tail stops decaying. The tone filter is
what saves you: the darker the feedback path, the higher the feedback can go
before it screams, because each pass loses top end. `feedback` above 1 self-
oscillates by design — legitimate as a drone source, but put a `limiter` or
a `drive { mode: soft }` after it.

**`reverb` freeze.** `freeze` mutes the input and makes the tank lossless.
It's a gate, so the base plus cable must land above 0.5:

```text
frz = lfo { freq: 0.05hz, wave: square }
0.5 -> verb.freeze
frz -> verb.freeze : 0.5            # 0 or 1, ten seconds each
```

A breakdown is one freeze gate and nothing else.

**Shimmer.** There is **no pitch-shift module**. Fake it with `grains` set to
`pitch: 1` (one octave up, plain resampling) placed in the reverb's feedback
path — the tail is re-granulated an octave up, re-injected, and shifted again
on the next pass. Cycles through `delay`/`reverb`/`grains` are legal.

```text
verb = reverb { decay: 0.88, damp: 0.3, mix: 1, predelay: 0.02 }
sv -> verb.in_l : 0.5
sv -> verb.in_r : 0.5
shift = grains { secs: 2, seed: 5, pitch: 1, size: 0.14, density: 26,
                 jitter: 0.4, shape: 0.8, spread: 0.7 }
verb.out_l -> shift.in : 0.5
verb.out_r -> shift.in : 0.5
shift.out_l -> verb.in_l : 0.34     # the loop gain: 0.34 blooms, 0.2 fades
shift.out_r -> verb.in_r : 0.34
```

`jitter` and `spread` above zero are load-bearing: a periodic grain stream
beats against itself metallically after two or three passes. `umbra.patch`
runs the non-recirculating version (bells → `grains { pitch: 1 }` → out) as a
halo; the feedback version is denser and much more dangerous.

**Flanger** — a short modulated delay with feedback, from `docs/modules.md`:

```text
fl_lfo = lfo { freq: 0.3hz }
fl = delay { time: 0.003, feedback: 0.55, mix: 0.5, slew: 0.0005, tone: 12000 }
sv -> fl.in
fl_lfo -> fl.time : 0.0025          # ±2.5 ms around a 3 ms base
```

Keep `slew` near zero or the time smoother smears the sweep into a wobble.
No through-zero and no negative-feedback hollow flange — the delay's
feedback is positive only.

**`chorus` and `phaser`.** `chorus` is stereo (feed a mono source to both
inputs to fan it out) and has no feedback — that's the flanger regime.
`phaser` is mono, and `mix: 0.5` is where the notches are deepest; `spread`
controls whether the stages are coincident (tight, Phase-90) or fanned
(vocal). Stack them: `phaser -> chorus` is `modfx.patch`.

```text
ph = phaser { stages: 8, spread: 0.4, rate: 0.22, depth: 0.7,
              feedback: 0.5, mix: 0.5 }
sv -> ph.in : 0.6
ch = chorus { voices: 3, rate: 0.35, depth: 0.55, mix: 0.45 }
ph -> ch.in_l
ph -> ch.in_r
```

**Feedback nets.** Two delays at incommensurate times, each feeding the
other, is a Berlin School counterpoint generator: a mono sequence becomes
polyrhythmic without a second sequencer.

```text
net_a = delay { time: 0.66s, feedback: 0.3, tone: 3200, mix: 1, slew: 0.4 }
net_b = delay { time: 0.99s, feedback: 0.3, tone: 2100, mix: 1, slew: 0.4 }
sv -> net_a.in : 0.4
net_a -> net_b.in : 0.5
net_b -> net_a.in : 0.5
net_sat = drive { amount: 0.05, mode: soft }
net_b -> net_sat.in
```

Loop gain is the product around the ring **plus** each delay's own
`feedback`. Here that's `0.5 · 0.5 = 0.25` in the cross path on top of 0.3
internal — comfortably stable. Push the cables to 0.8 each and the ring gain
is 0.64; add the internal 0.3 and it blooms.

## The knob that matters

**`delay.tone`.** It decides how many generations you get before the repeats
turn to mud, and mud is the point — that's the tape-echo character. 1200–2000
Hz gives three or four usable passes at high feedback; 8000 Hz (the default)
keeps repeats crisp and you'll hear the digital-ness immediately.

For `reverb` it's `damp`, for the same reason, one layer up.

## Modulating it

- `delay.time` under a tiny slow LFO (`: 0.006`, 0.017 Hz) with a large
  `slew` — repeats smear instead of lurching. This is the whole "tape" trick.
- `delay.feedback` under a glacial LFO at ±0.05: the echo field breathes in
  and out over a minute without any arrangement lane.
- `reverb.decay` and `damp` from separate incommensurate LFOs (0.011 and
  0.023 Hz) — the room changes size while you're in it.
- `grains.position`/`size` inside a shimmer loop reshape the tail from the
  inside; level automation alone reads as a fader.
- Send levels are an arrangement tool: `arr_lane -> mix.lvlN` on the return
  channels turns a section wet without touching the source.

## Gotchas

- **Zero `delay { time: }` before cabling `tempo` into it.** Every time.
- **Delay returns on their own mixer channel use `mix: 1`** (wet only).
  `mix < 1` is for inserts. A wet-only return double-counted as an insert is
  why the dry signal sounds smeared.
- **Feedback nets bloom slowly.** A ring that is stable for 30 seconds can be
  clipping at ten minutes; the level creeps a fraction of a dB per pass.
  Render the full length with `--stats 60s` and read the peak column, not the
  first window. The core patch above holds at −2.8 → −2.1 dBFS peak over
  180 s; anything with a rising trend will not survive a 20-minute set.
- **Tap sends post-fader.** A voice muted at the mixer still feeds a
  pre-fader send and leaves reverb ghosts. Gate the voice in its own `vca`,
  then send from that (`meridian.patch`).
- `reverb.freeze` is level-sensed, not edge-sensed: it needs a *held* value
  above 0.5.
- `grains.pitch` upward aliases, by design. That grit is part of the shimmer;
  if you don't want it, shift down instead.
- Long `slew` values (2–4 s) make `delay.time` modulation musical but mean the
  delay takes seconds to reach a newly cabled tempo — irrelevant in a render,
  audible on hot reload.

## Where it shows up

`dub-techno.patch` is the core patch's ancestor (two dark echoes, long plate,
kick-ducked returns). `mainstage.patch` runs the dotted-8th/straight-8th
lattice with zeroed time bases. `umbra.patch` builds a shimmer halo from
`grains { pitch: 1 }`. `modfx.patch` is the `phaser → chorus` chain.
`motorik.patch` freezes a `grains` buffer mid-song. Genre map: dub techno and
dub reggae (the chain above), Berlin School (cross-fed delays), ambient and
shoegaze (shimmer, freeze), psytrance and prog (dotted-eighth lattices).

## Push it further

1. Put a `compressor` in the shimmer feedback path before the `grains` — it
   keeps the recirculating tail dense instead of letting the loudest pass
   dominate, which is exactly what commercial shimmer units do.
2. Make the reverb decay follow the source: `follow { mode: rms }` on the
   stab into `reverb.decay : -0.2`, so the room shrinks while you play and
   opens in the gaps.
3. Cross-feed the two dub echoes into each other's inputs at 0.2 as well as
   their own feedback. The dotted-eighth grid starts generating notes that
   are on neither grid.
4. Send the delay return back into the *source's* filter cutoff via `follow`.
   The echoes reshape the next stab; the patch develops a memory.
5. Freeze the reverb and then keep playing into the `grains` shifter only —
   a frozen chord with a live shimmer over it, no dry path at all.
