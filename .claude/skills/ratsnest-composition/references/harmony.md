# Harmony: lanes, scales, tuning, voice leading

## The pitch domain

`A3v = 0.0`. One unit = one octave. Pitch is `220 × 2^v_oct` Hz.

A `…v` note name is an **absolute** pitch. An interval is a bare fraction —
there is no note-name spelling for "up a fifth". Keep the two straight and
transposition works everywhere.

### 12-TET interval constants

| Interval | v/oct | | Interval | v/oct |
|---|---|---|---|---|
| semitone | 0.08333 | | tritone | 0.5 |
| whole tone | 0.16667 | | fifth | 0.58333 |
| minor 3rd | 0.25 | | minor 6th | 0.66667 |
| major 3rd | 0.33333 | | major 6th | 0.75 |
| fourth | 0.41667 | | minor 7th | 0.83333 |
| | | | octave | 1.0 |

Cents: `cents / 1200`. ±12 cents ≈ `±0.01`. A 1 Hz beat at 110 Hz ≈ `0.0131`.

### Just intervals, when you want them to lock

| Ratio | Interval | v/oct |
|---|---|---|
| 3/2 | fifth | 0.58496 |
| 4/3 | fourth | 0.41504 |
| 5/4 | major 3rd | 0.32193 |
| 6/5 | minor 3rd | 0.26303 |
| 9/8 | major 2nd | 0.16993 |
| 7/4 | harmonic 7th | 0.80735 |
| 7/6 | septimal 3rd | 0.22239 |
| 11/8 | undecimal 4th | 0.45943 |

Just intervals stop beating; tempered ones shimmer. On drones that matters —
a just fifth is glassy and still, a tempered fifth pulses at ~0.6 Hz in the
bass. Pick on purpose. `patches/lib/sym-drone.sub` uses 0.58333 by default;
override `fifth: 0.58496` for the pure one.

## Chord lanes

Three bar-clocked `stepseq`s carrying absolute pitches. Every melodic element
in the patch reads from them, so the whole track re-harmonizes from three
arrays.

```text
root  = stepseq { steps: [A2v, E2v, F2v, G2v] }
third = stepseq { steps: [C3v, G2v, A2v, B2v] }
fifth = stepseq { steps: [E3v, B2v, C3v, D3v] }
bar -> root.clock
bar -> third.clock
bar -> fifth.clock
```

Feed the bass from `root`, a saw stack from all three, and the arp/lead from
a `switch` that steps among them (below). Voice the lanes so adjacent chords
move by step, not by leap — that is voice leading, and it is the difference
between a chord *progression* and four unrelated chords.

### Chord-tracking melody

`switch { mode: seq }` clocked at an eighth, stepping a fixed contour through
the (octave-lifted) chord lanes, is consonant over every chord by
construction:

```text
lift = math { op: add, b: 1 }        # +1 octave
root -> lift.a

mel = switch { mode: seq, n: 8 }
lift -> mel.in1
third -> mel.in2
fifth -> mel.in3
lift -> mel.in4
fifth -> mel.in5
third -> mel.in6
lift -> mel.in7
fifth -> mel.in8
eighth -> mel.clock

hold = sh { mode: sample }           # hold pitch through the decay
mel -> hold.in
eighth -> hold.trig
hold -> voice.v_oct
```

The `sh` matters: without it a ringing note retunes mid-tail when the switch
advances.

### Fixed phrases

A fixed phrase is fine **if it is pentatonic**. A minor-pentatonic hook has no
avoid notes over i–VI–III–VII or i–v–VI–VII; it is big, dumb and correct. If
your phrase contains a 2nd, 4th or 6th, it will clash on some chord — use the
chord-tracking switch instead, or change the chords.

## Quantizer scales

`quant { scale: [...], root: 0, transpose: 0 }`, semitones, empty = chromatic.

| Scale | Array | Character |
|---|---|---|
| natural minor | `[0,2,3,5,7,8,10]` | default sad |
| dorian | `[0,2,3,5,7,9,10]` | minor with a lifted 6th — techno, dub |
| phrygian | `[0,1,3,5,7,8,10]` | Spanish/dark; the b2 is the whole flavour |
| lydian | `[0,2,4,6,7,9,11]` | floating, unresolved major |
| mixolydian | `[0,2,4,5,7,9,10]` | rock/kosmische major |
| harmonic minor | `[0,2,3,5,7,8,11]` | that augmented-2nd leap |
| minor pentatonic | `[0,3,5,7,10]` | safest melodic source |
| major pentatonic | `[0,2,4,7,9]` | bright, folk, Reich-ish |
| blues | `[0,3,5,6,7,10]` | the b5 passing tone |
| whole tone | `[0,2,4,6,8,10]` | no gravity — dream sequences |
| octatonic | `[0,2,3,5,6,8,9,11]` | symmetrical, unstable, IDM |
| hirajoshi | `[0,2,3,7,8]` | Japanese pentatonic, wide gaps |
| insen | `[0,1,5,7,10]` | sparse, modal, beautiful over drones |
| just intervals | `[0,2,4,5,7,9,11]` (major) | see tuning note below |

**Fewer notes = more musical randomness.** A `random` into a chromatic
quantizer sounds like a fault; the same source into `[0,3,5,7,10]` sounds
composed. When a generative line is unconvincing, shrink the scale before
touching anything else.

`quant.root` shifts the scale in semitones, `quant.transpose` shifts the
output in v/oct. Automating `root` from a bar-clocked `stepseq` modulates the
key without retuning the pattern.

## Progressions that work here

| Progression | Feel | Used by |
|---|---|---|
| i (one chord, forever) | drone, dub techno, Berlin school | `dub-techno`, drones |
| i – VI – III – VII | anthemic, resolves | `ion` variants |
| i – v – VI – VII | rising, never resolves — peak-time | `ion` |
| i – VI – iv – VII | melancholy loop | `permafrost` |
| i – VII – VI – VII | modal rock/kosmische | `motorik` |
| I – V – vi – IV | pop, avoid unless deliberate | — |
| i – bII (phrygian) | menace, one move | dark ambient |

For ambient, **the absence of a progression is a decision, not laziness.** One
chord held for eight minutes with the timbre in motion is a form. Two chords
alternating on an irrational period is another.

## Polychords and overlap

Two adjacent diatonic chords sounding at once make an open, ambiguous dyad
stack. `permafrost` gets this for free: its freeze relay holds the previous
chord while the live drone plays the next one, so Em+C, C+Am, Am+D and D+Em
overlap at every block boundary. Any long-tail voice (`string` with high
damping, a frozen `grains`, a long `reverb`) does the same thing — lean into
it by choosing chord pairs that stack well rather than fighting the smear.

## Drones and beating

- **Detune for motion**: two `vco`s at 219 Hz and 221 Hz beat at 2 Hz. Detune
  via the `freq` base or a small `v_oct` constant, never by editing note names.
- **Stack fifths and octaves**, not thirds, for a drone bed. Thirds fix the
  mode; open fifths let a melody imply it.
- **Beat frequency is a tempo.** Éliane Radigue's method: choose the detune so
  the beat is a musical period — 0.25 Hz for a 4-second pulse. `0.0033` v/oct
  at 110 Hz gives ~0.25 Hz.
- **Sub-harmonics**: an octave-down sine under a `modal` fundamental thickens
  without muddying, because it adds no partials.

## Rhythm as harmony

Euclidean patterns with coprime step counts against the bar produce phasing
melodies from one pitch source: `euclid { steps: 7 }` on an eighth clock in
4/4 realigns every 7 bars. Two of them at 5 and 7 realign every 35. This is
harmony in time; the same "no two cycles align" logic as the glacial LFOs.
