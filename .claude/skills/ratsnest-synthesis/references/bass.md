# Bass — the loudest thing in the track, and the narrowest

## When to reach for it

Whenever the low end is the hook rather than the foundation: acid lines,
808 glides, reese, neuro growls, dub weight. If you just need a root note
under a pad, that's a sine and an `adsr` — skip to "Sub" below and stop.

Bass is one octave of pitch and three decisions: how much harmonic content
sits above the fundamental, what moves it, and how it gets out of the kick's
way.

## Core patch

An acid line: resonant `ladder`, envelope squelch, and an **accent lane that
hits both the level and the envelope depth** — that dual routing is the 303's
"gimmick circuit" and the reason acid sounds alive instead of sequenced.

```text
master  = clock { bpm: 480 }        # sixteenths at 120
eighth  = div { by: 2 }
master -> eighth.in

notes = stepseq { steps: [A1v, A1v, C2v, A1v, G1v, A1v, E2v, D2v],
                  gates: [1, 0, 1, 1, 1, 0, 1, 1] }
eighth -> notes.clock

# Slide: func with nothing on trig slews toward its `in`. Short = 303 glide.
glide = func { rise: 0.035, fall: 0.035 }
notes.cv -> glide.in

osc = vco { wave: saw, freq: 220hz }
glide -> osc.v_oct
sub = vco { wave: sine, freq: 220hz }
glide -> sub.v_oct

# Accent lane: its own stepseq, held over the note by an sh.
acc_pat = stepseq { steps: [1, 0, 0, 0.8, 0, 0, 1, 0] }
eighth -> acc_pat.clock
acc = sh {}
acc_pat.cv -> acc.in
notes.trig -> acc.trig

env = func { rise: 0.003, fall: 0.16, curve: exp }
notes.gate -> env.trig
# Accent widens the envelope: env * accent is a second, deeper sweep.
squelch = math { op: mult }
env -> squelch.a
acc -> squelch.b

filt = ladder { cutoff: 130hz, res: 0.86, drive: 0.5 }
osc -> filt.in : 0.8
env -> filt.cutoff : 900
squelch -> filt.cutoff : 1500

amp = vca { gain: 0, response: exp }
filt -> amp.in
env -> amp.cv : 0.75
acc -> amp.cv : 0.25

samp = vca { gain: 0 }
sub -> samp.in
env -> samp.cv : 0.8

grit = drive { amount: 0.12, mode: soft }
amp -> grit.in

# Bass stays mono: pan 0, no width, nothing stereo below it.
m = mix { levels: [1.1, 0.9], pans: [0, 0] }
grit -> m.in1
samp -> m.in2
m.out_l -> out.left
m.out_r -> out.right
```

Note `notes.gate -> env.trig`, not `notes.trig` — the `gates:` array holds
the rests, so muted steps don't retrigger and the slide has somewhere to
happen. And the oscillator base is `220hz` so the `…v` note names land at
written pitch (see `docs/patch-craft.md`); with `freq: 55hz` the whole line
drops two octaves silently.

## Variations

**808 glide bass** — one long sine, `func` slewing the pitch, no filter at
all. Everything comes from the glide time and the saturation.

```text
g_seq = stepseq { steps: [A1v, A1v, F1v, C2v] }
bar -> g_seq.clock
g_glide = func { rise: 0.08, fall: 0.08 }
g_seq.cv -> g_glide.in
g_osc = vco { wave: sine, freq: 220hz }
g_glide -> g_osc.v_oct
g_env = func { rise: 0.004, fall: 1.6, curve: exp }
g_seq.trig -> g_env.trig
g_amp = vca { gain: 0 }
g_osc -> g_amp.in
g_env -> g_amp.cv
g_sat = drive { amount: 0.14, mode: soft }
g_amp -> g_sat.in
```

Rise and fall should be equal or the glide is asymmetric up vs down.
`0.08` is a slur; `0.25` is the trap portamento; below `0.01` it's a step.

**Reese** — two saws detuned by a few cents (the detune amount *is* the
beating rate) into a **swept notch**. There is no notch module: subtract
`svf.bp` from the dry signal. `math { op: sub }` has `b: 1` by default and
cables sum onto it, so **you must write `b: 0`** or you get a −0.24 DC offset
and a mystery thump.

```text
r_a = vco { wave: saw, freq: 220hz }
r_b = vco { wave: saw, freq: 220hz }
g_seq.cv -> r_a.v_oct
g_seq.cv -> r_b.v_oct
0.006 -> r_b.v_oct                  # ~7 cents
r_sum = mix { levels: [0.5, 0.5] }
r_a -> r_sum.in1
r_b -> r_sum.in2
r_band = svf { cutoff: 500hz, res: 0.62 }
r_sum.out_l -> r_band.in
r_sweep = lfo { freq: 0.13hz, wave: tri }
r_sweep -> r_band.cutoff : 320
r_notch = math { op: sub, b: 0 }
r_sum.out_l -> r_notch.a
r_band.bp -> r_notch.b : 0.9        # depth of the notch
r_lp = ladder { cutoff: 1.1khz, res: 0.15, drive: 0.4 }
r_notch -> r_lp.in
```

`phaser { mix: 0.5 }` is the cheap version — several moving notches instead
of one, swept by its own LFO. Use it when you want the movement and don't
care where the notch is; use the subtraction when you want to place it.

**Neuro** — an FM growl layered *over* a reese, with the modulator level
under a near-audio-rate LFO. `fm.lvl1..lvl6` are inputs, so the FM index is
an automation target; that is what makes it talk.

```text
n_v = fm { algo: dual, freq: 220hz, ratios: [1, 2.01, 7, 1, 1, 1],
           levels: [0.8, 0.35, 0.2, 0, 0, 0],
           t2: [0.4, 0.1, 0.08, 0, 0, 0], l2: [0.8, 0.5, 0.3, 0, 0, 0],
           l3: [0.7, 0.4, 0.2, 0, 0, 0], curve: exp }
g_seq.cv -> n_v.v_oct
n_gate = func { rise: 0.002, fall: 1.2 }
g_seq.trig -> n_gate.trig
n_gate -> n_v.gate                  # fm.gate needs a gate, not a one-sample trig
n_warp = lfo { freq: 5.9hz, wave: tri }
n_warp -> n_v.lvl3 : 0.28           # the "vowel"
n_wob = lfo { freq: 2hz, wave: sine }
n_filt = ladder { cutoff: 320hz, res: 0.5, drive: 0.7 }
n_v -> n_filt.in : 1.6
n_wob -> n_filt.cutoff : 900        # the wobble
```

The ratio `2.01` rather than `2` is deliberate: the slow beat against the
carrier keeps the growl from sounding frozen.

**Dub bass** — detuned saws through a low `ladder` plus a sine sub at the
same pitch, gated by a `euclid` so the pattern breathes instead of playing
on every beat. The filter env is small (260 Hz of sweep); dub bass is round,
not squelchy.

```text
d_pat = euclid { steps: 8, hits: 3, rot: 0 }
clk -> d_pat.clock
d_filt = ladder { cutoff: 180hz, res: 0.22, drive: 0.5 }
d_a -> d_filt.in : 0.4
d_b -> d_filt.in : 0.4              # 0.004 v/oct apart
d_sub = vco { wave: sine, freq: 220hz }
d_env = func { rise: 0.012, fall: 0.34, curve: exp }
d_pat.trig -> d_env.trig
d_env -> d_filt.cutoff : 260
d_amp = vca { gain: 0 }
d_filt -> d_amp.in
d_sub -> d_amp.in : 0.7
d_env -> d_amp.cv
```

**Sub, when that's all you need.** A sine at the root and nothing else.
`ladder { cutoff: 120, res: 0.1 }` after it isn't filtering anything audible
— it's there so the saturation upstream doesn't leak harmonics. See
`mainstage.patch`'s `sub_osc`/`sub_filt` pair.

## The knob that matters

**Filter envelope depth**, expressed as the cable gain into `cutoff`. The
`ladder { cutoff: N }` config is the *floor* of the sweep, not the sweep —
set it to the darkest note you want and scale the env cable up from there.
130 Hz base + 900 env is an acid squelch; 180 + 260 is dub; 240 + 0 is a
reese that only moves when the notch does.

Second knob: `res`. Above ~0.9 on `ladder` the resonance eats the
fundamental — compensate with the sine sub layer, not with more gain.

## Modulating it

- Accent into *two* destinations (level and env depth). One is boring.
- Slow LFO on the reese detune (`0.05 Hz` into `v_oct : 0.006`) — the
  beating rate itself drifts.
- `ladder.drive` is an input: push it on drops for extra grind without
  touching cutoff.
- Sidechain: `kick_env -> mix.lvlN : -0.24`. `mix` level CVs are additive to
  `levels:`, so a lane fade and a kick duck on the same channel just sum.
- For the wobble, a *delayed* LFO reads better than a constant one — sum a
  slow `func` (rise 0.3 s, triggered by the note) into the LFO depth path
  via a `math { op: mult, b: 0 }`.

## Gotchas

- **`math` defaults both inputs to 0.** `op: mult` with nothing on `b` is
  silence; write `b: 1` (or patch `b`) when you mean "pass through".
- **Note names land at written pitch on the default 220 Hz base.** `freq: 55hz`
  with `A1v` is two octaves down.
- **Mono below ~120 Hz.** `pans: [0, 0]`, no `chorus`, no `stereo` width on
  the bass channel. Width goes on the growl layer above 250 Hz, never on the
  sub. Check with `stereo.corr` if you're unsure.
- **High-pass everything else, not the bass.** `eq { hp_freq: ... }` on pads,
  arps, and hats at 80–200 Hz makes room; a high-pass on the bass itself just
  makes it thin. The kick and the sub share 30–100 Hz and must be ducked
  against each other, not filtered apart.
- `drive { amount: 0.1 }` is already ~2x into the curve. On a bass that's
  glue; at 0.3 it's a fuzz pedal.
- Free-ringing pitched sources (`string`, `modal`) will ghost through a muted
  section. `func`/`adsr`-driven VCAs won't. Bass wants the VCA.

## Where it shows up

`mainstage.patch` (reese + sub pair, `bass_lvl` lane driving cutoff and amp
together), `dub-techno.patch` (the restrained filtered kick/bass economy),
`patches/lib/bass.sub` (FM bass → `ladder` → `overdrive`, the reusable
version), `ion.patch`/`halcyon.patch` for chord-lane-tracked roots. Genre
map: acid → Chicago/Detroit techno, reese → DnB and jungle, neuro →
dubstep/neurofunk, 808 glide → trap and modern electro, dub bass → dub
techno and its ambient descendants.

## Push it further

1. Drive the acid accent lane from a `bern` instead of a `stepseq` — the
   squelch lands somewhere different every bar but at the same density.
2. Feed the reese's notch cutoff from a `follow` on the kick: the notch
   ducks out of the way rhythmically instead of on a timer.
3. Build the neuro layer as a `voice` group over `alloc { n: 2 }` so
   overlapping notes each keep their own FM envelope through the tail.
4. Run the sub through `grains { pitch: -1, size: 0.3 }` in parallel at low
   level for a sub-octave that tracks without a divider.
5. Put the whole bass through `overdrive { model: rat, mix: 0.4 }` and
   high-pass the wet path at 200 Hz — distortion for the harmonics, clean
   fundamental underneath.
