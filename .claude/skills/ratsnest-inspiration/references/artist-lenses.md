# Artist lenses

Not "sound like X". *Work the way X worked* — the method transfers, the sound
doesn't. Each lens is a documented working practice (see
`docs/research/2026-07-20-modular-ambient-technique.md` and
`docs/research/2026-07-20-genre-synthesis-technique.md`) plus its ratsnest
translation.

---

## Éliane Radigue — the beat is the composition

**Method.** Two ARP 2500 oscillators tuned within a fraction of a Hz, moved "by
no more than a hair" over hours. She never touched the keyboard: keyboards
induce "piano habits". The *battement* — the beating between near-unisons — is
the entire musical material.

**The lens.** Delete every trigger from your patch. Two oscillators, one
detune, and one very slow modulation of that detune. The piece is the beat rate
changing.

```text
a = vco { freq: 110hz, wave: tri }
b = vco { freq: 110hz, wave: tri }
0.0028 -> b.v_oct                     # ~0.21 Hz: a five-second pulse
crawl = lfo { wave: sine, freq: 0.0021hz }   # 476 s
crawl -> b.v_oct : 0.0016             # the beat rate itself breathes
```

**The discipline.** No new elements. If it's boring, the modulation is too fast,
not too small.

---

## Brian Eno — incommensurable loops

**Method.** *Music for Airports*: tape loops of 20.1 s and 19.6 s, harmonically
compatible in any combination, set running and left alone. "They are not likely
to come back into sync again." He wanted "a silence at least twice as long as
the sound."

**The lens.** Choose four periods that share no small common multiple. Give each
one voice and a long rest. Never let two share a clock.

```text
l1 = lfo { wave: saw, freq: 0.0498hz }    # 20.1 s
l2 = lfo { wave: saw, freq: 0.0510hz }    # 19.6 s
l3 = lfo { wave: saw, freq: 0.0327hz }    # 30.6 s
```

Use `logic { op: cmp }` against a constant to turn each ramp into a rare gate,
then strike a `modal` or `lpg` voice with it. The silence between is the point:
if the events overlap constantly, raise the comparator thresholds.

---

## Suzanne Ciani — space is a parameter

**Method.** "Quadraphonic in electronic language is meant to be a parameter like
pitch and volume and rhythm... it's not about the sound, but the way the sound
moves." Panning is composed and modulated exactly like cutoff.

**The lens.** Patch the position before the timbre. Two `mix` channels per voice
at opposite pans, crossfaded by an LFO; or `stereo.balance` under a slow ramp.
The movement is the melody.

```text
mv = xfade { mix: 0.5 }
voice -> mv.a
voice -> mv.b
orbit = lfo { wave: sine, freq: 0.043hz }
orbit -> mv.mix : 0.5
```

Give each voice a different orbit period. Nothing should arrive anywhere at the
same time as anything else.

---

## Caterina Barbieri — one source, derived everything

**Method.** The sequencer is "the brain of the setup": arithmetic and geometric
pattern operators generate hundreds of variants, and a separate gate track
selects which *portion* of a stored pattern actually sounds. *Patterns of
Consciousness* uses one oscillator for the entire record, deriving melody by
filtering into its partials.

**The lens.** One `stepseq`, one voice. Vary `len` from a lane so the phrase
length changes; gate portions in and out with a second, coprime sequence; derive
the harmony by resonant filtering rather than by adding notes.

```text
seq = stepseq { steps: [0, .25, .5833, .75, 1, .5833, .25, .0833] }
len_lane = stepseq { steps: [8, 5, 8, 3, 8, 6] }
bar -> len_lane.clock
len_lane.cv -> seq.len
```

**The discipline.** You may not add a second voice. Solve everything with the
one you have.

---

## Alessandro Cortini — the instrument as bandmate

**Method.** Music Easel patches built around a single-oscillator drone gated by
the pulser into the low-pass gate. Deliberately minimal; the instrument is "an
empty canvas" and a collaborator, not an executor of a prior idea.

**The lens.** `vco -> lpg`, one modulation source striking it, and nothing else.
Then spend the whole session on the two knobs you have.

```text
tone = vco { freq: 110hz, wave: tri }
gate = lpg { damp: 0.4, decay: 0.6 }
tone -> gate.in
pulse = lfo { wave: square, freq: 1.7hz }
pulse -> gate.strike
```

Add `fold` between them only when you've exhausted what `damp` and `decay` can
do.

---

## Kaitlyn Aurelia Smith — follow what the machine wants

**Method.** Patching as discovering "what the machine seemed like it wanted to
do, rather than what she wanted it to do". Environmental interference audibly
changes the patch; she leans in.

**The lens.** Build something, render it, and then change the patch toward
whatever surprised you rather than toward your plan. In ratsnest the equivalent
of environmental drift is the seed: change one `seed:` and treat the result as
the machine's suggestion.

---

## Steve Roach / Robert Rich — the mixer is the arrangement

**Method.** Layered sequencers of mismatched lengths, phased and re-lengthened
live, with slow filter/VCA/EQ moves on reverb sends as the only "arrangement" —
"the magic hour of sunset, how the light shifts over 20 minutes". *Structures
From Silence* is one 30-minute take, shaded in real time.

**The lens.** No new material after the first minute. Everything after is send
levels, filter cutoffs and reverb decay under multi-minute ramps.

```text
dusk = lfo { wave: tri, freq: 0.0018hz }    # ~9 minutes
dusk -> send.lvl3 : 0.35
dusk -> pad_filt.cutoff : 700
dusk -> hall.decay : 0.15
```

---

## Sarah Davachi / Kali Malone — tuning as the subject

**Method.** Just-intonation systems shared between synthesizer and acoustic
instruments so beating and overtones reinforce. Davachi: "setting two
oscillators in tune and listening to them endlessly drift in and out of one
another."

**The lens.** Build the drone from just ratios only, and let the piece be about
which interval is sounding.

```text
root  = vco { freq: 110hz, wave: tri }
fifth = vco { freq: 110hz, wave: tri }
third = vco { freq: 110hz, wave: tri }
0.58496 -> fifth.v_oct        # 3/2
0.32193 -> third.v_oct        # 5/4
```

Introduce the third forty seconds in and remove it two minutes later. That is
the whole form.

---

## Basic Channel / Deepchord — the decay is the composition

**Method.** One chord stab per bar, deliberately soft, into saturation → filtered
delay near self-oscillation → plate → low-pass → sub reinforcement, cycling, so
the tail of one hit becomes the attack of the next perceived event. Tape hiss
kept in on purpose.

**The lens.** Make the dry signal quiet enough to be a rumour. Spend your effort
on `delay.tone`, `delay.feedback` and `reverb.damp`. If you can hear the stab
clearly, turn it down.

---

## Boards of Canada — two layers of instability

**Method.** The synths are detuned against themselves, *and* everything is
bounced across tape machines to accumulate wow, flutter, hiss and generational
loss. Two independent kinds of wrongness stacked.

**The lens.** Detune your voices, *then* put a very slow `random` on a `delay`'s
`time` (a few milliseconds of swing) and run the whole mix through it at
`mix: 0.15`. Add pink noise at −45 dB and never take it out.

```text
wow = random { rate: 0.7hz, smooth: 1, seed: 5 }
tape = delay { time: 0.02, feedback: 0, mix: 0.15, slew: 0.05 }
wow -> tape.time : 0.0012
```

---

## Klaus Schulze / Tangerine Dream — transpose, don't rewrite

**Method.** One locked 8- or 16-step sequence, transposed in real time from a
keyboard, through cross-fed delay so the repeats interleave into
quasi-polyphony. The filter opens over minutes and that is the arrangement.

**The lens.** Write one sequence. Never edit it again. All change comes from a
bar-clocked transpose lane, a cross-delay pair, and a cutoff that takes four
minutes to open.

```text
seq = stepseq { steps: [0, .25, .4167, .5833, .75, .5833, .4167, .25] }
trans = stepseq { steps: [0, 0, -0.1667, -0.1667, .0833, .0833, -0.4167, 0] }
bar -> trans.clock
trans.cv -> seq.transpose
```

---

## Hainbach — break the machine

**Method.** Uses failure modes as sources: a cassette deck's bias oscillator,
audible only when pushed past its intended speed range. "My goal is to always
make my own sounds and try to break the machines."

**The lens.** Find ratsnest's honest failure modes and compose with them.
Upward `grains.pitch` aliases by design. Rich `fm` `waves` alias when pushed. A
`delay` above feedback 1 self-oscillates. `math { op: wrap }` on audio is a
fault that sounds like an instrument. `pulse` at `pw: 0.05` carries DC that a
low-pass passes straight through — normally a bug, occasionally a bass.

---

## Using a lens

Pick one. Obey it for one whole patch, including the parts you disagree with.
Then write in the header comment which lens it was and what you kept.
