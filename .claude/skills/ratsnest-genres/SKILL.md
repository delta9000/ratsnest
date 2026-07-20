---
name: ratsnest-genres
description: Use when a ratsnest patch is asked for by style — techno, house, dub techno, acid, trance, drum and bass, jungle, breakcore, IDM, ambient, drone, dark ambient, Berlin School, kosmische, motorik, synthwave, dubstep, neuro — or when a request names an artist, a record, a club, a mood with a genre attached.
---

# Genre cards

Each card is a **departure point, not a destination**. It tells you the tempo,
the clock tree, which ratsnest modules do the genre's signature work, the
arrangement shape, and the mix targets — so you can get a convincing version
running in twenty minutes and then spend your time on the thing that makes it
yours.

Load `ratsnest-patching` first; `ratsnest-composition` for form and mix numbers;
`ratsnest-synthesis` for the voices the cards call for.
Source research: `docs/research/2026-07-20-genre-synthesis-technique.md`.

## The cards

| Card | BPM | The one thing that makes it that genre |
|---|---|---|
| `references/ambient.md` | none | drone plus sparse events; nothing resolves |
| `references/dark-ambient-drone.md` | none | rumble is low frequency *plus grain*; crescendo by unmasking |
| `references/berlin-school.md` | 90–140 | a locked sequence, transposed live, through cross-delay |
| `references/kosmische-motorik.md` | 120–140 | a beat that never varies against a pad that never moves |
| `references/dub-techno.md` | 115–130 | the chord's decay is the composition |
| `references/techno-acid.md` | 120–135 | accent widens the filter envelope; slide suppresses the retrigger |
| `references/deep-house.md` | 118–124 | 7ths and 9ths, never triads; the pump is the groove |
| `references/trance-prog.md` | 132–140 | strip → rebuild → lift, on a 32-bar grid |
| `references/breaks-jungle.md` | 160–180 | chop the break, layer weight underneath, half-time the feel |
| `references/idm.md` | any | the process makes the pattern; the tape degrades it |
| `references/synthwave.md` | 80–118 | gated reverb snare, arpeggiated bass, PWM lead |
| `references/bass-music.md` | 140 / 172 | a delayed LFO on a resonant filter; FM index as a formant |

## How to use a card

1. Read it once. Build the skeleton exactly as written and `check` it.
2. Render 30 s, read `--stats`, fix the levels.
3. **Then break one rule on purpose.** Every card ends with a "how to break it"
   section for exactly this. A patch that follows a card perfectly is a genre
   exercise; a patch that follows it and then does one wrong thing is a track.

## Genres are recipes, not identities

The cards overlap heavily, and that's the useful part:

- Dub techno's filtered-delay chain works on an ambient bell just as well as on
  a chord stab.
- The Berlin School "one sequence, transposed" trick is how you get melodic
  motion out of a generative patch without writing a melody.
- Motorik's separation of *motion* (drums) from *stasis* (pad) is the structural
  skeleton of half the ambient techno ever made.
- The 808 hi-hat (six inharmonic squares → high-pass) is a bell if you drop the
  high-pass and lengthen the envelope.

If a request doesn't name a genre, don't pick one. Ask what it should *feel*
like and build from `ratsnest-inspiration` instead.

## Cross-genre defaults

If a card doesn't say otherwise:

- Clock tree from one `clock` at 4× tempo through `div { by: 2 }`.
- Bass mono below 150 Hz, one element in the sub slot at a time.
- Duck at three depths: channel, FX return, bus glue comp.
- Phrase in 4/8/16/32-bar blocks; break the grid once, deliberately.
- Kick channel ~0.85, premaster peak ≈ −3 dBFS, limiter at −0.4.

Ambient and drone are the exception to the last one. See
`ratsnest-composition/references/mastering.md`.
