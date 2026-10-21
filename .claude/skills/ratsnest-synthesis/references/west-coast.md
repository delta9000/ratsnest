# West coast — add harmonics instead of removing them

## When to reach for it

When a subtractive voice sounds like a synth patch and you want something
that sounds like an *object being struck*. Buchla's design premise was that
brightness should come from driving a waveshaper harder, not from opening a
filter — so loudness and timbre move together, the way they do in every
physical instrument. That is why folded-and-gated voices read as percussive,
organic and slightly unstable even when the notes are boring.

Reach for it for bongo-ish percussion, evolving drones, generative ambient
beds, and anything where you want an LFO on *one* parameter to change the
whole character. Reach elsewhere if you need a stable, mixable pad — folding
is a moving target and hard to gain-stage.

## Core patch

Complex oscillator (one `vco` phase-modulating another) into a `fold` whose
depth is under an envelope, into an `lpg` struck by the clock. The
`sym` input drifts under a glacial LFO so the fold never repeats exactly.

```text
# west coast core -- complex oscillator into a folder into a struck LPG.
clk = clock { bpm: 104 }
sparse = div { by: 3 }
clk -> sparse.in

wander = random { rate: 0.9, smooth: 0, seed: 17 }
step = sh { mode: sample }
wander -> step.in
clk -> step.trig
key = quant { scale: [0, 2, 5, 7, 9] }
step -> key.in : 0.35

modu = vco { freq: 220hz, wave: sine }
prin = vco { freq: 220hz, wave: tri }
key -> modu.v_oct
key -> prin.v_oct
-1 -> modu.v_oct
modu -> prin.fm : 0.22

folder = fold { fold: 1.4, sym: 0 }
prin -> folder.in

timbre = func { rise: 8ms, fall: 0.5, curve: exp }
clk -> timbre.trig
timbre -> folder.fold : 2.6
drift = lfo { freq: 0.043hz, wave: sine }
drift -> folder.sym : 0.25

gate = lpg { damp: 0.3, decay: 0.35 }
folder -> gate.in
clk -> gate.strike
sparse -> gate.cv : 0.12

verb = reverb { decay: 0.72, damp: 0.35, mix: 0.28 }
gate -> verb.in_l
gate -> verb.in_r
verb.out_l -> out.left : 0.55
verb.out_r -> out.right : 0.55
```

Three things to notice. `-1 -> modu.v_oct` puts the modulator an octave below
the principal and both track the quantizer, so the FM ratio stays fixed as
the melody moves. `timbre -> folder.fold : 2.6` is the west-coast move: the
same trigger that opens the gate also drives the folder harder, so every note
starts bright and *darkens as it fades*. `sparse -> gate.cv : 0.12` leaves a
low residual glow under the plucks on every third step.

## Variations

**Buchla bongo.** The canonical patch: a plain sine, a folder swept by a very
short envelope, an `lpg` with a ~90 ms decay. No noise, no filter, no
oscillator pitch modulation.

```text
body = vco { freq: 220hz, wave: sine }
fld = fold { fold: 1.0 }
body -> fld.in
hit = func { rise: 0.001, fall: 0.09, curve: exp }
hit -> fld.fold : 2.4
skin = lpg { damp: 0.55, decay: 0.09 }
fld -> skin.in
clk -> skin.strike
```

Raise `damp` toward 0.8 for a duller, more rubber-headed mallet. Send `hit`
into `body.v_oct : 0.2` and you have a tom.

**Two oscillators through Min/Max into the folder.** The forum-standard drone
patch: two detuned triangles combined with `math { op: min }` (a hard
nonlinearity in its own right), then folded.

```text
a = vco { freq: 220hz, wave: tri }
b = vco { freq: 331hz, wave: tri }
sum = math { op: min }
a -> sum.a
b -> sum.b
fld = fold { fold: 2.2 }
sum -> fld.in : 1.4
drift = lfo { freq: 0.031hz }
drift -> fld.fold : 1.6
```

`min` is asymmetric and carries DC (measured +0.033) — `dcblock` after the
folder. `max` is the mirror image; `add` is tamer and more predictable.

**Ring modulation.** `math { op: mult }` with an audio-rate second input.
Sum and difference frequencies only — no fundamental survives, which is why
it reads as metallic and detached from the played pitch.

```text
voice = vco { freq: 220hz, wave: tri }
carrier = vco { freq: 317hz, wave: sine }
rm = math { op: mult }
voice -> rm.a
carrier -> rm.b
blend = xfade { mix: 0.6 }
voice -> blend.a
rm -> blend.b
```

Leave the carrier at a fixed Hz (do not track pitch) and the inharmonicity
changes across the keyboard — that is the sound. Track it and you get a
static, harmonic timbre instead. `xfade` back a little dry signal or the
result has no pitch centre.

**Amplitude modulation.** Same idea but the modulator stays unipolar, so the
carrier survives: a `vca` with a nonzero base gain and a bipolar LFO on top.

```text
amp = vca { gain: 0.5 }
voice -> amp.in
trem -> amp.gain : 0.5
```

At `trem` under ~20 Hz that is tremolo. Push it to 60–400 Hz and the
sidebands become the timbre, with the carrier still audible underneath.
Ring mod is this patch with the base gain at 0.

**LPG as sustain, not strike.** `lpg.cv` (clamped 0..1) holds the gate open
continuously — an LFO on `cv` is a filter-and-VCA sweep in one cable.

```text
g = lpg { damp: 0.7, decay: 1.2 }
0.45 -> g.cv
open -> g.cv : 0.35
```

`strike` and `cv` coexist: the strike ramps the envelope to 1 (over 2 ms,
fast enough to pluck, slow enough not to click) and it decays
back down toward whatever `cv` holds. That floor is how you get plucks
sitting on top of a drone through one gate.

**The unstable element.** West coast patches are supposed to make decisions
you did not. A `bern` splitting the clock into "struck" and "held", plus a
slow `random` on both pitch and fold:

```text
coin = bern { prob: 0.35, seed: 9 }
clk -> coin.gate
jitter = random { rate: 0.11hz, smooth: 1, seed: 4 }
jitter -> osc.v_oct : 0.04
jitter -> fld.fold : 1.1
coin.a -> g.strike
coin.b -> g.cv : 0.25
```

Seeds make it repeatable, which is the only way this is usable in a rendered
piece. Change the seed, not the structure, when you don't like a take.

## The knob that matters

**`fold` depth, and specifically what is driving it.** The folder computes
`in * fold + sym` and reflects the result into ±1, so harmonic content is a
direct function of input amplitude. Below `fold: 1.0` with a ±1 input nothing
folds at all and you have a triangle. From 1 to about 3 you are adding
harmonics steadily; past 5 the fundamental starts disappearing into a buzz
that no longer tracks pitch usefully.

Static `fold` is a wasted module. The value of the technique is entirely in
what moves it: an envelope (percussive), a slow LFO (drone), or the note's
own velocity.

## Modulating it

- `folder.fold` from the note envelope — brightness follows loudness, the
  whole point.
- `folder.sym` from a glacial LFO at ±0.25. Asymmetry adds *even* harmonics,
  which is a different colour entirely from more folding, and it moves the
  fold points off the zero crossings so the timbre shimmers rather than just
  brightening.
- `lpg.damp` under an LFO: how dark the decay is, independent of how long.
- FM index (`modu -> prin.fm` depth) from a second envelope with a longer
  fall than the amplitude envelope — the tone gets brighter *after* the
  attack.
- `random { smooth: 1 }` at 0.05–0.2 Hz onto two or three of these at once,
  at different depths. Free-running, unsynced, deliberately incommensurate.

## Gotchas

- `fold` has **no configs** — `fold` and `sym` are inputs, so everything in
  the braces is a base that cables sum onto. `fold` clamps negative to 0
  (silence), so a bipolar LFO straight into it with no base cuts out for half
  its cycle. Always give it a base.
- Anything asymmetric — `sym`, `math { op: min/max }`, an off-centre pulse
  into the folder — produces DC. Measured −0.0009 on the core patch with
  `sym` drifting; more with `min`. `dcblock` before the master bus.
- `lpg.damp` is inverted from intuition: **low damp = bright.** Cutoff is
  `30 * 2^(env * (9 - 6*damp))` Hz, so `damp: 0` peaks near 15 kHz and
  `damp: 1` near 240 Hz.
- `lpg.decay` is a one-pole time constant in seconds, not a linear fall
  time; it never quite reaches zero, so a struck LPG bleeds quietly forever.
  Combined with `patch-craft.md`'s ghost-note warning, gate it before its
  sends.
- `lpg` attack is fixed at ~2 ms and not configurable. If you need a slower
  swell, drive `cv` from a `func` rather than using `strike`.
- `vco.fm` is scaled by the `freq` base, not the played pitch, so a
  complex-oscillator FM index drifts as you transpose. Patching the same
  pitch CV into both oscillators (as the core patch does) fixes the *ratio*
  but not the index — accept it or drive `prin.fm` from a `vca` whose gain
  tracks pitch.
- Ring mod output can exceed ±1 when both inputs are hot. Stage it down at
  the cable; `math` does not clamp.

## Where it shows up

Buchla 200/Music Easel and its descendants: Alessandro Cortini's Easel work
(one oscillator drone, pitch from the keyboard, envelope from the pulser into
the LPG), Kaitlyn Aurelia Smith's 100/200e patches, Kali Malone's just-tuned
Buchla sine drones, Suzanne Ciani's quad Buchla performances. Modern
descendants: Make Noise DPO/Optomix, Verbos, Instruō. In this repo:
`patches/west-coast.patch` is the reference (folded triangle, vactrol plucks,
noise ring-modulated by the raw oscillator), and `docs/research/
2026-07-20-modular-ambient-technique.md` §2 has the sourced background.

## Push it further

- Cross-modulate: patch `prin -> modu.fm` as well as `modu -> prin.fm`. The
  feedback loop goes chaotic at high depth and glassy at low depth, and the
  boundary between them is the interesting region.
- Two folders in series with a `math { op: mult }` between them, the second
  fed a different envelope. Serial folding is not the same as more folding.
- Strike the LPG from a `bern` at `prob: 0.15` off a fast clock instead of a
  regular pulse — Buchla bongos that decide their own rhythm.
- Run the folder output into `modal.in` instead of the LPG. Wavefolded
  excitation into a physical resonator is a whole sound family neither card
  covers on its own.
- Use `follow` on the folded signal and patch its `env` back into `fold.fold`
  at a small negative depth: a compressor made of west-coast parts, where
  loud passages fold *less*.
