# Subtractive — start rich, carve it away with a moving filter

## When to reach for it

Anything that has to sound like a synthesizer and sit in a mix: bass, lead,
pad, brass stab, string machine, acid line, chiptune. It is the default
because it is controllable — pitch, brightness and loudness are three
separate knobs, and each one takes an envelope.

Reach elsewhere when you want a struck body (`modal`/`string`), a metallic
inharmonic spectrum (`fm`), or timbre that brightens with *amplitude* rather
than a cutoff (`fold`, see `west-coast.md`).

## Core patch

Two detuned saws plus a sub octave, a `ladder`, and *two* `adsr`s — one for
the filter, one for the amp. Portamento from a `func` with nothing on `trig`.

```text
# subtractive core -- two detuned saws + sub, ladder, two envelopes.
clk = clock { bpm: 200 }
mel = stepseq { steps: [A2v, C3v, E3v, G3v, A3v, G3v, E3v, C3v] }
clk -> mel.clock

# portamento: func with nothing on trig slews toward its `in`
gl = func { rise: 0.12, fall: 0.12 }
mel.cv -> gl.in

o1 = vco { freq: 220hz, wave: saw }
o2 = vco { freq: 220hz, wave: saw }
sub = vco { freq: 110hz, wave: pulse }
gl -> o1.v_oct
gl -> o2.v_oct
0.007 -> o2.v_oct
gl -> sub.v_oct

amp_env = adsr { attack: 6ms, decay: 0.18, sustain: 0.5, release: 0.22, curve: log }
flt_env = adsr { attack: 3ms, decay: 0.16, sustain: 0.15, release: 0.18, curve: log }
mel.gate -> amp_env.gate
mel.gate -> flt_env.gate

vcf = ladder { cutoff: 160, res: 0.42, drive: 0.4 }
o1  -> vcf.in : 0.4
o2  -> vcf.in : 0.4
sub -> vcf.in : 0.3
flt_env -> vcf.cutoff : 3000

amp = vca { gain: 0, response: exp }
vcf -> amp.in
amp_env -> amp.cv : 0.9

amp -> out.in : 0.7
-0.1 -> out.level
```

Read the staging: `cutoff: 160` is the *darkest* the voice ever gets, and
`: 3000` is how far the envelope lifts it. Cables sum onto bases — the config
number is the floor of the sweep, never the sweep amount. Set the floor by
ear first with the env cable at 0, then dial the cable depth.

Use `mel.gate`, not `mel.trig`, on an `adsr`: `trig` is one sample and the
sustain stage never happens.

## Variations

**Supersaw.** Seven saws, detune spread ±0.0175 v/oct (±21 cents at the
edges), panned outward, the centre saw loudest. Detune in `v_oct` and not
`freq` so note names keep landing.

```text
s1 = vco { freq: 220hz, wave: saw, v_oct: -0.0175 }
s2 = vco { freq: 220hz, wave: saw, v_oct: -0.0105 }
s3 = vco { freq: 220hz, wave: saw, v_oct: -0.0035 }
s4 = vco { freq: 220hz, wave: saw }
s5 = vco { freq: 220hz, wave: saw, v_oct: 0.0035 }
s6 = vco { freq: 220hz, wave: saw, v_oct: 0.0105 }
s7 = vco { freq: 220hz, wave: saw, v_oct: 0.0175 }
saws = mix { levels: [.3, .22, .22, .34, .22, .22, .3],
             pans: [-.9, -.55, -.25, 0, .25, .55, .9] }
```

Uneven spacing matters — evenly spaced detunes beat in a single audible
period. Widening the spread to ±0.04 turns it from "wide" into "chorused and
out of tune"; that is the whole range you have.

**PWM.** A slow `lfo` on `pwm` (which sums onto the `pw` base, clamped
0.05..0.95) makes one pulse oscillator breathe like an ensemble.

```text
pad = vco { freq: 110hz, wave: pulse, pw: 0.5 }
sweep = lfo { freq: 0.13hz, wave: tri }
sweep -> pad.pwm : 0.42
dcb = dcblock { cutoff: 20 }
pad -> dcb.in_l          # measured DC +0.055 without this
```

The `dcblock` is not optional. Off-centre pulse carries a DC offset of about
`2*pw - 1`, low-pass filters pass it straight through, and it eats headroom
and shoves the limiter around. Read `dcb.out_l`.

**Hard sync lead.** A master oscillator resets the slave's phase every cycle;
sweeping the slave's pitch moves the formant, not the note.

```text
mas = vco { freq: 110hz, wave: pulse }
slv = vco { freq: 110hz, wave: saw }
mas -> slv.sync
sweep = func { rise: 0.01, fall: 0.9, curve: exp }
clk -> sweep.trig
sweep -> slv.v_oct : 2.2
```

Sync truncates the slave waveform mid-cycle, so it carries DC too (measured
−0.018) — `dcblock` it. The master must swing through 0.5, so use `pulse` or
`saw`, never a quiet sine.

**Filter FM.** An audio-rate oscillator into `ladder.cutoff` in Hz, not into
`in`. Sidebands from the filter itself: metallic, unstable, unlike anything
the oscillator can make.

```text
vcf = ladder { cutoff: 300, res: 0.7 }
fmo = vco { freq: 220hz, wave: sine }
fmo -> vcf.cutoff : 900
```

Depth is in Hz and does *not* track pitch, so the timbre changes across the
keyboard. Patch the same pitch CV into `fmo.v_oct` if you want it to track.
Expect DC (measured −0.028) and clean it up downstream.

**Acid.** One saw, `res` near 0.9, a short `func` on both cutoff and amp,
plus `drive` inside the filter.

```text
env = func { rise: 0.002, fall: 0.16, curve: exp }
acidf = ladder { cutoff: 90, res: 0.88, drive: 1.2 }
env -> acidf.cutoff : 2600
```

`ladder.drive` blends in `tanh(in * (1 + 4*drive))` before the ladder, so it
saturates *into* the resonance instead of after it. That is the squelch.

**Pinged filter.** No oscillator at all — a sub-millisecond noise burst into a
high-resonance filter is a sine with a decay envelope built in.

```text
burst = func { rise: 0.0002, fall: 0.002 }
ex = vca { gain: 0 }
hiss -> ex.in
burst -> ex.cv
bell = ladder { cutoff: 660, res: 0.97 }
ex -> bell.in : 1.0
```

Ring time is set entirely by `res`: 0.97 decays ~90 dB/s (measured at 220 Hz);
1.0 never decays at all (see Gotchas). See `percussion.md` for drum use.

## The knob that matters

**Filter envelope depth** — the `: 3000` on `flt_env -> vcf.cutoff`. Every
other parameter shapes a static tone; this one decides whether the voice has
an attack. At depth 0 the patch is an organ. At 1000 it is a plucked keys
patch. At 4000 with `res: 0.8` it is an acid line. Change nothing else and
the voice changes category.

Second place goes to `res`, which controls *what kind* of brightness the
sweep sounds like rather than how much.

## Modulating it

- Cutoff base under a glacial `lfo` (0.02–0.05 Hz, depth 200–600 Hz) so the
  floor of the sweep itself moves over minutes.
- `res` under a slower one still. Resonance moving is the difference between
  a filter and a filter *sweep*.
- Detune: `evo -> o2.v_oct : 0.02` — beats that wander instead of sitting.
- `pw`/`pwm` on the sub, even 0.1 deep, kills the "one oscillator" flatness.
- Velocity or an accent lane into `flt_env`'s depth: `acc -> vcf.cutoff : 1200`
  on top of the envelope cable makes accented notes brighter, not just louder.
- `amp` is the least interesting thing to modulate. Do it last.

## Gotchas

- Both filters **self-oscillate at the top of `res`**: `ladder` from about
  0.93, `svf` from about 0.97, into a clean sine that reaches ±1 at `res: 1`
  and sits within a cent of the cutoff. Below the edge they ring and decay;
  above it they sing forever with no input. For a decaying ping stay under
  0.92 (`ladder`) or 0.96 (`svf`); at 1.0 either one is a sine oscillator —
  patch pitch into `v_oct` and it plays in tune. Both saturate, so a loud
  input at resonance stays within a few units instead of exploding.
- `svf` below 0.9 has the same Q curve it always had (−6 dB at the cutoff at
  0, unity at 0.5, +14 dB at 0.9); `ladder` partly compensates its passband
  loss (−7 dB at 0.5, −4.5 dB at 0.9, where an uncompensated ladder loses
  13 dB). See `docs/modules.md` for the measured tables.
- `vco.fm` is *relative linear* FM scaled by the `freq` base, not the current
  pitch: `f = freq * 2^v_oct + fm * freq`. Depth stays constant in Hz as you
  play up the keyboard, so a fixed FM index gets relatively weaker up high.
  Use `v_oct` for anything that must track.
- `func`'s `rise`/`fall` are seconds per **1.0 unit**, so as a portamento
  slew, `rise: 0.12` glides a whole octave in 120 ms and a semitone in 10 ms.
  Bigger leaps take proportionally longer — that is correct portamento
  behaviour, not a bug.
- Never patch anything to that glide `func`'s `trig`. One rising edge and it
  stops being a slew limiter and becomes an envelope, permanently.
- `vca { response: exp }` squares the gain, so an envelope peaking at 0.9
  gives 0.81. Compensate at the cable, not by pushing the envelope past 1.
- `adsr` releases and retriggers from the *current* level, so fast repeated
  notes do not click but also do not reach full level.

## Where it shows up

Everything from 1970s onward: Moog/Roland bass and leads, trance supersaws,
acid 303 lines, string machines, film-score brass pads. In this repo:
`patches/keys.patch` is the reference two-envelope voice, `patches/berlin.patch`
runs PWM and relative FM with no filter at all (Phase 1a constraint, still
instructive), `patches/pluck.patch` uses a plucked `ladder` for its bass, and
the bass card (`bass.md`) takes the acid and reese variants further.

## Push it further

- Give each supersaw voice its own `lfo` at an incommensurate rate (0.031,
  0.043, 0.057 Hz) into its `v_oct` at ±0.004. The detune stops being a
  constant and becomes weather.
- Two `ladder`s in series with slightly different cutoffs and only one under
  the envelope: an eight-pole slope that still has a moving formant.
- Feed `svf.bp` instead of `.lp` and sweep with an envelope for a vocal wah;
  cross-fade `lp` and `hp` with `xfade` under an LFO for a moving notch.
- Sync the master oscillator to the *sequencer clock* instead of another
  oscillator (`clk -> osc.sync`) — every note starts at identical phase, which
  is how you get a kick-tight attack transient with no click.
- A `res: 1.0` ladder needs no input at all: it is a ±1 sine at the cutoff.
  Track it with `v_oct`, sweep `cutoff`, and feed it a little of the bass so
  the oscillation bends against the input — treat it as a drone source and
  duck it with the kick.
