# Berlin School — one sequence, moved under your hands for twenty minutes

## The feel

A pattern locks and then refuses to stop, and everything interesting happens
*around* it: the filter opens a hair per bar, echoes interleave until you
can't tell which notes you played, and the whole thing lifts a fourth without
a single note changing. It is hypnotic rather than danceable — the pulse is
constant but there is no groove to speak of.

## Clock

90–140 BPM, straight, one clock, no swing. Run the master at sixteenths and
take the sequence off eighths so you keep a faster division in hand for
ratchets. Pieces are 15–30 minutes as one continuous form; the div chain runs
out to 16 bars because that is where transposition happens.

```text
master      = clock { bpm: 448 }       # sixteenths at 112 BPM
eighth      = div { by: 2 }
quarter     = div { by: 2 }
bar         = div { by: 4 }
four_bar    = div { by: 4 }
sixteen_bar = div { by: 4 }
master -> eighth.in
eighth -> quarter.in
quarter -> bar.in
bar -> four_bar.in
four_bar -> sixteen_bar.in
```

## Palette

| Element | Modules | Starting values |
|---|---|---|
| the sequence | `stepseq` of 16 `…v` note literals | eighth-clocked, `dir: forward` |
| transposition | second `stepseq` → `seq.transpose` | 16-bar clock, `[0, 0, -0.41667, -0.16667]` |
| voice | two `vco { wave: saw }` at `freq: A3`, `0.004` detune | note names need an A base |
| filter | `ladder { cutoff: 220, res: 0.42, drive: 0.5 }` | base = darkest point of the arc |
| envelope | `func { rise: 0.004, fall: 0.16, curve: exp }` | fires from `seq.trig` |
| counterpoint | two `delay { mix: 1 }` at 3/8 and 1/8, cross-fed | `feedback` 0.6–0.66 |
| the arc | `lfo { freq: 0.0021hz }` → `ladder.cutoff` | 8-minute cycle, depth 1900 |
| pad | sustained saws under it, no envelope | one chord, or none |
| space | `reverb { decay: 0.8, mix: 1 }` on its own channel | plate, not hall |

## Signature moves

### 1. The sequence never changes; the root does

This is the whole genre in one cable. Schulze's Synthanorma played the same
sixteen steps for a side of vinyl while he transposed the pattern from a
keyboard. `stepseq.transpose` is that keyboard: a slow lane of v/oct offsets
summed onto every step at once. The pattern's *shape* is what the ear
memorises; moving it wholesale reharmonises everything without touching a
note.

```text
seq = stepseq { steps: [A2v, A3v, E3v, A2v, C3v, E3v, G3v, E3v,
                        A2v, A3v, E3v, A2v, D3v, E3v, C3v, B2v] }
eighth -> seq.clock
transp = stepseq { steps: [0, 0, -0.41667, -0.16667] }   # i i iv VII
sixteen_bar -> transp.clock
transp.cv -> seq.transpose

lead  = vco { freq: A3, wave: saw }
lead2 = vco { freq: A3, wave: saw }
seq.cv -> lead.v_oct
seq.cv -> lead2.v_oct
0.004 -> lead2.v_oct

vcf = ladder { cutoff: 220, res: 0.42, drive: 0.5 }
lead  -> vcf.in : 0.4
lead2 -> vcf.in : 0.4
env = func { rise: 0.004, fall: 0.16, curve: exp }
seq.trig -> env.trig
env -> vcf.cutoff : 900
lead_amp = vca { gain: 0 }
vcf -> lead_amp.in
env -> lead_amp.cv : 0.7
```

Transposition offsets are bare fractions, never note names — `-0.41667` is
down a fourth from wherever the step already is. Note-name literals in the
`steps` array only land at written pitch because `freq: A3` anchors the
oscillator (`docs/patch-craft.md`, "note names need a 220 Hz base").

### 2. Cross-delay makes counterpoint out of a monophonic line

Two delays at different beat divisions, panned apart and feeding *each other*,
turn sixteen steps into a texture that sounds contrapuntal and is not. Dotted
eighth on one side, straight eighth on the other: the repeats never line up
with each other and only line up with the sequence every three bars.

```text
t_dot = tempo { bpm: 112, beats: 0.75 }
t_eig = tempo { bpm: 112, beats: 0.5 }
dly_l = delay { time: 0ms, feedback: 0.6,  mix: 1, tone: 3200, slew: 0.05 }
dly_r = delay { time: 0ms, feedback: 0.64, mix: 1, tone: 2600, slew: 0.05 }
t_dot.seconds -> dly_l.time
t_eig.seconds -> dly_r.time
lead_amp -> dly_l.in : 0.45
lead_amp -> dly_r.in : 0.4
dly_l -> dly_r.in : 0.22        # the cross-feed IS the counterpoint
dly_r -> dly_l.in : 0.22
```

`time: 0ms` on both — cables sum onto the base, so leaving the default 0.4
would add 400 ms to your dotted eighth. Pan the returns hard.

### 3. Fifteen against sixteen

Reich phasing with gear instead of tape: two sequencers on the same clock, one
a step shorter. They realign every 240 eighths — thirty bars of slowly
rotating relationship out of one pattern typed twice. `len` is an *input*, so
the phasing length itself is an automation target.

```text
sq_b = stepseq { steps: [A2v, A3v, E3v, A2v, C3v, E3v, G3v, E3v,
                         A2v, A3v, E3v, A2v, D3v, E3v, C3v, B2v] }
eighth -> sq_b.clock
15 -> sq_b.len
transp.cv -> sq_b.transpose
voice_b = vco { freq: A3, wave: tri }
sq_b.cv -> voice_b.v_oct
1 -> voice_b.v_oct              # an octave up, quieter
env_b = func { rise: 0.002, fall: 0.1, curve: exp }
sq_b.trig -> env_b.trig
amp_b = vca { gain: 0 }
voice_b -> amp_b.in
env_b -> amp_b.cv : 0.35
```

### 4. The filter is the arrangement

There is no drop. The single dynamic gesture of the genre is a VCF travelling
from nearly closed to bright over many minutes and back. Put it on a glacial
LFO rather than a lane so it never lands on a bar line, and move resonance
with it — brightness alone reads as a fade, brightness plus Q reads as a
machine warming up.

```text
arc = lfo { wave: tri, freq: 0.0021hz }   # ~8-minute round trip
arc -> vcf.cutoff : 1900
arc -> vcf.res : 0.12
```

### 5. Ratchets from a faster division and a gate lane

The sequence runs on eighths; AND the sixteenth clock with a bar-clocked lane
and those bars fire twice per step. Because both come from the same div tree
it is phase-locked by construction — no second sequencer, no timing drift.

```text
rat_lane = stepseq { steps: [0,0,0,0, 0,0,0,1, 0,0,0,0, 0,1,0,0] }
bar -> rat_lane.clock
rat = logic { op: and }
master -> rat.a
rat_lane.cv -> rat.b
rat -> env.trig                 # sums with seq.trig: double-time on those bars
```

## Arrangement

One arc, no sections in the dance sense. Write it as blocks anyway so the
render duration is calculable.

```text
# Arrangement (16 sixteen-bar blocks, 34.3 s each at 112 BPM):
#   0-1    intro     pad only, non-metric, no sequence
#   2-3    lock      sequence enters filtered near-closed, no delays
#   4-5    open      delays in, cross-feed up, filter starts climbing
#   6-7    phase     sq_b enters at len 15
#   8-10   peak      filter arc at top, resonance up, ratchets appear
#   11-12  drop out  sequence alone, delays muted, filter closes
#   13-14  return    everything back, transposition lane moves to iv
#   15     tail      sequence stops; delays and plate ring out
```

At 112 BPM a 16-bar block is 34.3 s, so this is a ~9-minute piece; double the
block length for a proper side-long one. Guard the wrap: lane length × block
seconds must exceed your render duration (`ratsnest-composition/
references/form.md`).

## Mix targets

| Slot | Owner |
|---|---|
| 40–100 Hz | sequence root octave when it dips there; otherwise empty |
| 100–400 Hz | pad body, sequence fundamentals |
| 400–2k | the sequence, dry, centre — it must stay legible |
| 2k–6k | filter resonance peak, ratchet transients |
| 6k+ | delay hiss, plate tail, hard-panned |

Drop-window RMS **−16 to −18 dB**, crest 14 dB+. Keep the dry sequence at the
centre and the two delay returns at ±0.85 — the width *is* the counterpoint.
Watch cumulative feedback: two cross-fed delays at 0.6 plus a 0.8-decay plate
can bloom minutes into a render. Check `--stats` peak over the full duration,
not a 30 s probe.

## In this repo

- `patches/berlin.patch` — the Phase 1a original: three octave-related square
  LFOs binary-weighted onto `v_oct` make an eight-step staircase with no
  sequencer at all. Steal the trick; it costs three modules and never drifts.
- `patches/meridian.patch` — the eight-lane tracker grid. Berlin School only
  needs two lanes of it (transposition and filter), but the wiring is there.
- `patches/aurora.patch` — `quant.changed` striking a voice only when the note
  actually moves, so repeats become ties. Ideal for a sequence you don't want
  articulated every step.
- `patches/fathom.patch` — glacial-LFO discipline for the filter arc.
- `patches/dub-techno.patch` — the two-delay + plate topology, already tuned;
  swap the stab for your sequence and you are most of the way there.

## How to break it

1. **Stop transposing and change one note instead.** A single step edited
   every sixteen bars is a slower, stranger kind of motion than moving the
   whole pattern, and nobody does it.
2. **Put the ratchet lane on `random` instead of a stepseq.** `bern { prob:
   0.12 }` gating the AND makes the double-time arrive unpredictably; the
   piece stops being a machine and starts being played.
3. **Make the delays fight.** Push the cross-feed to 0.45 with `tone: 900`.
   It will self-oscillate into a drone; ride `feedback` from a lane and use it
   as a section rather than an accident.
4. **Let the sequence stop for eight bars.** The genre never does this. When
   only the delays remain, the pattern you hear is one nobody typed.
5. **Detune the second voice by a comma, not a cent.** `0.0179` (a syntonic
   comma, ~21.5 cents) instead of `0.004` puts the phasing voice in an
   audibly wrong tuning; over fifteen minutes it reads as harmonic depth
   rather than as a mistake.

## Listening

- **Tangerine Dream, *Phaedra*** — the cross-delayed Moog sequence over a
  17-minute side. Steal the drift: their sequencer was not stable, and the
  slow detune is why it breathes.
- **Tangerine Dream, *Rubycon*** — the filter-opening arc at its most
  patient. Nothing enters for four minutes.
- **Klaus Schulze, *Moondawn*** — real-time transposition of a locked pattern.
  This card's move 1 is literally this record.
- **Klaus Schulze, *Timewind*** — one sequence, 30 minutes, almost no chord
  changes. Study how much variation lives in filter and echo alone.
- **Ashra, *New Age of Earth*** — sequence plus guitar; the counterpoint is
  played, not delayed. A good model for what to add on top.
- **Node, *Node*** — the modern hardware-modular version. Steal the mix: dry
  sequence dead-centre, everything else in a much wider, wetter field.
- **Redshift, *Ether*** — Berlin School with menace; slower tempos, darker
  filters, and the transposition lane used to drop rather than lift.
- **Manuel Göttsching, *E2-E4*** — one hour, one loop, one chord change.
  The endpoint of "the sequence never changes".
