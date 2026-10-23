# FM voices — six sine operators, and the envelopes are the instrument

## When to reach for it

Anything metallic, glassy, wooden, bell-like or plucked-and-inharmonic:
electric pianos, tubular bells, mallets, DX brass, digital basses that cut
through a mix at low level, growls, woodblocks. Also when a subtractive voice
is too *smooth* — FM's attack transient is built from operator envelopes
rather than a filter, so it can be brighter at the start than at the sustain
in a way no `ladder` sweep imitates.

Not for warm analogue pads (detuned saws), and not when you need one obvious
brightness knob to sweep live — FM brightness lives in six envelopes at once.

## Core patch

An electric piano, played by a `stepseq`, with its timbre under slow
modulation and a dotted-eighth echo. Note that `lvl3` and `fbmod` are
**inputs** — the timbre moves without anything touching the amplitude.

```text
# fm core -- 6-op electric piano, with timbre under modulation.
clk = clock { bpm: 190 }
mel = stepseq { steps: [A2v, E3v, C3v, G3v, A2v, D3v, F3v, C3v] }
clk -> mel.clock

ep = fm { algo: dual, freq: 220hz,
          ratios: [1, 1, 14, 1, 3.5, 1],
          levels: [.55, .30, .12, .40, .18, 0],
          fb: [0, 0, 0, 0, .36, 0],
          vel_sens: [.3, .7, .8, .3, .7, 0],
          t1: [.001, .001, .001, .001, .001, .001],
          t2: [.60, .09, .05, .50, .06, .10],
          t3: [1.4, .35, .15, 1.2, .25, .30],
          t4: [.30, .25, .15, .30, .20, .20],
          l1: [1, 1, 1, 1, 1, 1],
          l2: [.60, .28, .18, .55, .20, .20],
          l3: [.20, 0, 0, .16, 0, 0],
          key_level: [0, -.35, -.6, 0, -.35, 0],
          key_rate: [.3, .3, .3, .3, .3, 0],
          curve: log }
mel.cv -> ep.v_oct
mel.gate -> ep.gate

# timbre is an automation target: bell partial and feedback breathe
tine = lfo { freq: 0.037hz, wave: sine }
tine -> ep.lvl3 : 0.10
bite = lfo { freq: 0.023hz, wave: tri }
bite -> ep.fbmod : 0.11

amp = vca { gain: 0 }
ep -> amp.in
0.8 -> amp.gain

echo = delay { time: 0ms, feedback: 0.42, tone: 3200, mix: 0.26 }
t = tempo { bpm: 190, beats: 0.75 }
t.seconds -> echo.time
amp -> echo.in

echo -> out.in : 0.8
```

`freq: 220hz` is load-bearing: note names are v/oct from A3 = 0, so an
oscillator whose base is not an A transposes everything. `delay { time: 0ms }`
is load-bearing too — cables sum onto bases, so the default 0.4 would add
itself to the tempo.

## How the module actually works

**Routing.** An edge a→b adds a's post-envelope output to b's phase. Ops with
no outgoing edge are carriers, and `out` is their straight sum.

| `algo` | edges | shape |
|---|---|---|
| `free` | none | six independent carriers — additive sine stack |
| `triad` | 2→1, 4→3, 6→5 | three 2-op voices summed; struck partials |
| `dual` | 3→2→1, 6→5→4 | two 3-op stacks; the EP/bass workhorse |
| `cascade` | 6→5→4→3→2→1 | one deep chain; extreme index, growls |
| `crown` | 2..6→1 | five modulators on one carrier; formant-ish |
| `weave` | 6,5→4→2, 3→2, 2→1 | branching; complex and hard to predict |
| `custom` | `route: [2,1, 3,2]` | flat from/to pairs, 1-based |

`route` pairs must be integral 1..6, no self-edges, no duplicates, acyclic. A
malformed route silently falls back to `free` (the session never dies) — so a
voice that suddenly sounds like an organ means your `route` is broken.

**Envelopes.** Each op has four segments, not ADSR. On the gate's rising edge
the level runs 0 → `l1` over `t1`, → `l2` over `t2`, → `l3` over `t3`, then
holds `l3`. On the gate's fall it runs to 0 over `t4`, always from wherever it
currently is. Defaults are `t = [.002, .1, .4, .2]`, `l = [1, .75, .5]`.

The percussive idiom is `l1: 1, l2: 0, l3: 0` with `t2` as the decay time and
`t3` tiny — the note dies whether or not you release the gate. The sustained
idiom is `l3` nonzero. `curve: exp` is a true RC fall whose segment time is
the −60 dB time — write about 5x what a linear fall would need, since
brightness follows the index nonlinearly (measured on `fm-tests`); `log` is
a gentler fast-start fall; `lin` is right for brass and pads; `quad` (x²)
hangs near the top and then drops.

**Everything else.** `fb[k] > 0` is op *k*'s self-edge (never put a self-edge
in `route`); 0 is a pure sine, ~0.6 a saw-like 1/n spectrum, ~0.8 the
brightest clean saw, and only past ~0.9 does it break into noise. It acts on
the op's own waveform, so the same `fb` gives the same timbre whatever the
op's `levels` entry (velocity and `lvl` don't touch it; the envelope does). `fixed`
pins an op to an absolute Hz, decoupled from `v_oct` and `ratio` — clicks and
body resonances that don't transpose. `waves` swaps the sine for
`half`/`abs`/`quarter`/`fold`, which alias by design. `vel_sens` scales op
level by `vel`. `key_level` thins ops up the keyboard (negative), `key_rate`
shortens envelopes up high (positive); both latch from `v_oct` at the gate
edge. Levels are 0..1, and a level-1 modulator at envelope peak drives ±4π
radians (index ≈ 12.6) — so a modulator at 0.3 is already a lot of FM.

## Variations

Seven complete config blocks. Each is verified; drop it in place of `ep` in
the core patch. All assume `freq: 220hz` and a `gate`/`v_oct` feed.

**Tubular bell** — `triad`, three struck inharmonic 2-op pairs, everything
decaying to nothing over seconds.

```text
v = fm { algo: triad, freq: 220hz,
         ratios: [1, 3.51, 2.02, 7.11, 4.03, 9.34],
         levels: [.45, .35, .30, .28, .22, .18],
         t1: [.002, .002, .002, .002, .002, .002],
         t2: [4.5, 2.6, 3.4, 1.7, 2.8, 1.2],
         t3: [.01, .01, .01, .01, .01, .01],
         t4: [.6, .4, .5, .3, .4, .25],
         l1: [1, 1, 1, 1, 1, 1],
         l2: [0, 0, 0, 0, 0, 0],
         l3: [0, 0, 0, 0, 0, 0],
         curve: log }
```

The non-integer ratios are the whole trick, and the *different* `t2` per op
is what makes it a bell rather than a chord: high partials must die first.

**Glassy pad** — `free`, i.e. no FM at all, just six sines at near-harmonic
ratios with staggered multi-second attacks. Needs a long gate (a `div`-slowed
clock or a held `mel.gate`), or you never hear past `t1`.

```text
v = fm { algo: free, freq: 220hz,
         ratios: [1, 2.003, 3, 4.005, 6, 8.01],
         levels: [.40, .26, .18, .14, .09, .06],
         fb: [.24, 0, 0, 0, 0, 0],
         t1: [1.2, 1.6, 2.1, 2.6, 3.2, 3.8],
         t2: [2.0, 2.0, 2.0, 2.0, 2.0, 2.0],
         t3: [3.0, 3.0, 3.0, 3.0, 3.0, 3.0],
         t4: [2.5, 2.5, 2.5, 2.5, 2.5, 2.5],
         l1: [1, .9, .8, .7, .6, .5],
         l2: [.8, .7, .55, .45, .35, .25],
         l3: [.7, .55, .4, .3, .2, .12],
         curve: lin }
```

The `.003` and `.005` on ratios 2 and 4 beat slowly against the pure ones;
the lone `fb: .24` on the fundamental keeps it off "test tone".

**Brass** — `dual` at ratio 1:1, and the modulator's attack *slower* than the
carrier's (`t1` 0.11 vs 0.05). Brightness arrives after the note does; that
lag is the entire brass gesture.

```text
v = fm { algo: dual, freq: 220hz,
         ratios: [1, 1, 1, 2, 1, 1],
         levels: [.60, .55, 0, .30, .40, 0],
         t1: [.05, .11, .001, .06, .13, .001],
         t2: [.25, .30, .1, .25, .30, .1],
         t3: [.5, .5, .4, .5, .5, .4],
         t4: [.22, .30, .2, .22, .30, .2],
         l1: [1, 1, 0, 1, 1, 0],
         l2: [.85, .62, 0, .85, .58, 0],
         l3: [.80, .50, 0, .78, .46, 0],
         curve: lin }
```

**Woodblock** — `custom` with one 2-op pair at an inharmonic 5.42, plus op 4
as a `fixed: 2600` Hz click that does not transpose. Every time is in
milliseconds.

```text
v = fm { algo: custom, route: [2, 1], freq: 220hz,
         ratios: [1, 5.42, 1, 1, 1, 1],
         levels: [.75, .55, 0, .35, 0, 0],
         fixed: [0, 0, 0, 2600, 0, 0],
         t1: [.0005, .0005, 0, .0002, 0, 0],
         t2: [.075, .030, .1, .004, .1, .1],
         t3: [.005, .005, .4, .002, .4, .4],
         t4: [.02, .02, .2, .002, .2, .2],
         l1: [1, 1, 0, 1, 0, 0],
         l2: [0, 0, 0, 0, 0, 0],
         l3: [0, 0, 0, 0, 0, 0],
         curve: log }
```

**Bass** — `dual`, `fb: .68` on the modulator for saw-like grit, `curve: exp`
for a punchy fall, `key_level` negative on the modulators so it doesn't turn
into a buzzsaw when you play it an octave up.

```text
v = fm { algo: dual, freq: 220hz,
         ratios: [1, 1, 2, 1, 3.01, 1],
         levels: [.85, .45, .18, .30, .12, 0],
         fb: [0, .68, 0, 0, 0, 0],
         t1: [.001, .001, .001, .001, .001, 0],
         t2: [1.5, .23, .25, 1.5, .2, .5],
         t3: [4, .6, .5, 4, .5, 2],
         t4: [.5, .3, .3, .5, .3, 1],
         l1: [1, 1, 1, 1, 1, 0],
         l2: [.9, .35, .25, .8, .2, 0],
         l3: [.75, .12, .05, .6, .05, 0],
         key_level: [0, -.5, -.5, 0, -.5, 0],
         curve: exp }
```

**Growl** — `cascade`, feedback on three ops, rectified and folded operator
waveforms, ratios detuned off integers so nothing locks. Aliasing is part of
the sound.

```text
v = fm { algo: cascade, freq: 220hz,
         ratios: [1, 1.001, 2.99, 4.02, 1.5, 7.03],
         levels: [.8, .45, .30, .22, .16, .10],
         fb: [0, .88, .53, 0, .20, 0],
         waves: [sine, fold, abs, sine, quarter, sine],
         t1: [.004, .004, .004, .004, .004, .004],
         t2: [.2, .18, .16, .14, .12, .1],
         t3: [.6, .6, .6, .6, .6, .6],
         t4: [.12, .12, .12, .12, .12, .12],
         l1: [1, 1, 1, 1, 1, 1],
         l2: [.9, .7, .6, .5, .45, .4],
         l3: [.85, .6, .5, .4, .35, .3],
         curve: lin }
```

Sweep `fbmod` on this one: −0.3 relaxes the stack toward a vowel, and +0.1
already tips op 2 over its ~0.9 edge into chainsaw noise.

Any of these goes polyphonic unchanged inside a `voice` group bound to an
`alloc` — patch `poly.voct`/`poly.vel`/`poly.gate` into the stamped instance
and sum its output outside the group (`patches/fm.patch` is the worked
example, verified at four voices).

## The knob that matters

**Modulator level, and its envelope shape.** `ratios` decides *which*
inharmonic family you are in; the modulator's level envelope decides whether
the voice is a piano, a bell or a brass patch — with identical ratios. Decay
faster than the carrier and you get a bright attack that mellows (piano,
bell, mallet); rise more slowly and you get brass; hold flat and you get a
static, buzzy organ. If an `fm` voice sounds wrong, look at the modulator's
`t2`/`l2` before touching anything else.

## Modulating it

`lvl1..lvl6`, `ratio1..ratio6` and `fbmod` are audio-rate **inputs** that sum
onto their config bases. This is the module's best feature and the reason an
`fm` voice never has to sound static:

- `lfo -> v.lvl3 : 0.1` at 0.03 Hz — a bell partial fading in and out over
  half a minute.
- `env -> v.lvl2 : 0.25` from a *second* envelope shaped differently from the
  operator's own: index that doesn't follow the note.
- `lfo -> v.ratio5 : 0.02` — a partial detuning and re-locking. Tiny depths.
  Ratios are multipliers; `: 1.0` is an octave jump, not a nudge.
- `v.fbmod` broadcasts onto every op's `fb`. One cable takes the whole patch
  from clean to shredded.
- `follow -> v.lvl2` from another part in the mix, so the FM index responds
  to what the drums are doing.
- Tap `v.op4` (a modulator's post-envelope output) into another module's
  `fm` or `cutoff` input — the operator stack doubles as a modulation source.

## Gotchas

- **Unpatched `gate` = silence.** Nothing else in the module produces sound
  on its own. Use a `stepseq`'s `gate` (clock-width) — `trig` is one sample
  and the envelope release fires immediately.
- `levels` defaults to `[1,0,0,0,0,0]`: ops 2..6 are off until you ask. A
  routing change alone changes nothing if the modulator's level is still 0.
- `lvlN` inputs sum onto `levels[N]` and clamp to 0..1, so a bipolar LFO on
  an op that is already at 1.0 only ever subtracts.
- Segment *times* are seconds, not rates. `t2: [4.5, ...]` is a 4.5-second
  fall. `t: 0` is a one-sample jump, which is what you want for a click.
- `l3` nonzero means the note **holds forever** while the gate is high. For
  percussion set `l2`/`l3` to 0 and put the decay in `t2`.
- `fixed` beats `ratio`: any op with `fixed > 0` ignores `v_oct` and its
  `ratioN` input entirely.
- `key_level`/`key_rate` latch at the gate edge, so portamento into a new
  note does not re-scale until the next gate. Deliberate.
- `out` is a straight sum of carriers, unnormalised. Six carriers at level
  0.8 clip hard. Stage at the cable or with a `vca`.
- `sync: on` (the default) resets all phases on a gate that starts from
  silence (carriers below −60 dB) — that's why repeated notes are
  identical. A retrigger while the voice still rings keeps its phases, so
  it does not click. `sync: off` is looser on sustained parts.

## Where it shows up

DX7-era everything: 1980s electric pianos, bell pads, slap basses, tubular
bells, "Lately Bass". Modern: Elektron Digitone leads and percussion, FM
neuro-bass, Ableton Operator glass pads, plus FM drums (the wood/click recipe
above is a rimshot away from a percussion voice). In this repo:
`patches/fm.patch` is the reference generative EP played through a four-voice
`alloc`, and `docs/modules.md` §`fm` is the port authority.

## Push it further

- Automate `ratio2` with a `quant`ised random so the modulator jumps between
  *musically related* ratios (1, 1.5, 2, 3) rather than sliding — FM timbre
  as a sequence.
- Build a bell with `algo: free`, six partial ratios measured off a real
  recording, and `t2` per op as the decay-time spectrum. Additive synthesis
  inside an FM module, more exact than `modal` for a specific object.
- Give one op `fixed:` a value near 50–80 Hz and a 5 ms envelope. Instant
  body thump under any pitched voice, no layering.
- Run two `fm` instances with identical configs, one at `ratio1 + 0.002`, and
  pan them apart. FM detune sounds nothing like oscillator detune, because
  the sidebands move too.
