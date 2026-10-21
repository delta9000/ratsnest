---
name: ratsnest-patching
description: Use when writing, editing, or reviewing a ratsnest .patch or .sub file — any request to make a sound, a groove, a drone, a track, a voice, or a generative system in this repo. Entry point that routes to the other ratsnest skills.
---

# Patching ratsnest

The `.patch` file is the instrument. You cannot hear it, so you build with
your eyes and verify with numbers. Everything below assumes that.

## Read these first

1. `docs/modules.md` — the authoritative port/config reference. **Never guess
   a port name.** If it isn't in that file it doesn't exist.
2. `docs/patch-craft.md` — the gotchas that cost a render to learn.

Then load the skill that matches the job:

| You are asked for | Load |
|---|---|
| a timbre, voice, drum, texture, effect chain | `ratsnest-synthesis` |
| a self-playing / evolving / never-repeating patch | `ratsnest-generative` |
| a named genre, a track "like X", a groove | `ratsnest-genres` |
| a song with sections, a melody, a written-out score | a `.trk` file played by `tracker` (below) |
| structure, harmony, arrangement, mixing, mastering | `ratsnest-composition` |
| "surprise me", a starting point, a way out of a rut | `ratsnest-inspiration` |
| MIDI, polyphony, chords, live play, capture, writing a `.sub` | `ratsnest-instrument` |
| a patch that is silent, clipping, out of tune, or not what the comment claims | `ratsnest-debugging` |

Worked examples with verified numbers live in `patches/etudes/`:

| Etude | The one idea |
|---|---|
| `incommensurate` | five glacial LFOs on five destinations; no grid at all |
| `battement` | a drone built from beating; no triggers anywhere in the patch |
| `locked-loop` | one `turing` register drives pitch, rhythm *and* timbre |
| `coprime` | 5/7/11/13 Euclids; a 5-step pitch seq against a 7-step gate seq |
| `feedback-garden` | a dub ecosystem where every effect has a path home |

## The loop

```bash
mise run check  -- patches/x.patch                       # parse + port validation
mise run render -- patches/x.patch -d 30s -o /tmp/x.wav --stats 4s
```

`--stats` is your ears: a per-window peak/RMS/DC timeline. Read it as

- **silence** → a `vca` with `gain: 0` and nothing on `cv`, or an envelope
  never triggered, or a mixer channel you forgot to patch to `out`.
- **peak == RMS** → bricked; back off drive/limiter input.
- **section boundary in the wrong window** → your div chain or grid is off.
- **DC ≠ 0** → a `pulse` at `pw != 0.5`, an `abs`, or a folder. Add `dcblock`.

Iterate on numbers, not vibes. Two or three passes converges.

## Five rules that break patches

1. **Cables sum onto bases.** `time: 0.4` plus a `tempo` cable is 0.4 + tempo.
   Zero the base (`time: 0ms`) when a cable should own the input. A filter's
   `cutoff:` config is the *floor* of the sweep, not the amount.
2. **Note names are Hz; `…v` names are v/oct.** `A3v = 0.0`, and a `vco`
   computes `freq_base × 2^v_oct` — so a note name lands at its written pitch
   **only on a 220 Hz base** (the `vco`/`fm`/`additive` default, so leave
   `freq` unset). 110 is an octave down, 55 is two octaves down
   (inaudible, and `--stats` reads identical), 440 is an octave up. Reaching
   for `freq: 55hz` on a sub voice is the classic silent-bass bug. Intervals
   are bare fractions — `0.5833 -> osc.v_oct` is a fifth up. There is no
   note-name form for an interval.
3. **Nothing is normalled.** An unpatched input reads its declared default and
   nothing else. No hidden mixing, no hidden gain-staging, no `1/n`.
4. **Free-ringing voices ghost.** `string`, `lpg`, `modal`, `grains` keep
   sounding after their trigger. Gate the *strike*, not just the output, or a
   note swells in from a section that is supposed to be silent.
5. **Identifiers are lowercase.** Uppercase letters lex as note names.

## Shapes to reach for

- **Clock tree, not clocks.** One `clock` at 4× the tempo, then a chain of
  `div { by: 2 }`. Every rhythm in the patch is phase-locked for free, and
  "offbeat" is `logic{and}` of eighth with `logic{not}` of quarter.
- **Songs with a form: write a `.trk`.** Notes, sections and the arrangement
  go in a plain-text tracker file; the patch stays about sound.
  `docs/modules.md` `tracker` has the format, `docs/patch-craft.md` "Writing a
  song with tracker" the craft, `patches/tracker/` three worked songs (chords, groove, ambient).
- **Arrangement grid.** (Pre-tracker; still right for a lane of levels.) Bar-clocked `stepseq` lanes of 0..1 levels → `func`
  (nothing on `trig`, so it slews) → `vca.cv`. One lane per element. Asymmetric
  rise/fall is the trick: risers rise for seconds and fall in 0.4 s.
- **Chord lanes.** Three bar-clocked `stepseq`s carrying root/third/fifth as
  `…v` note names, feeding bass, pad, arp and lead alike. The whole track
  re-harmonizes by editing three arrays.
- **Subpatches for voices.** `patches/lib/*.sub` already has kick, bass, hats,
  a full physical kit, a sympathetic drone, and a freeze-cloud. Instantiate
  before you rebuild — their params, ports and *measured levels* are in
  `ratsnest-synthesis/references/lib-voices.md`.

## Common mistakes

| Symptom | Cause |
|---|---|
| Track comes back ~2 dB quiet | `out.level` scaled below 1; let the limiter own the ceiling |
| Delay time is wrong | forgot to zero the `time:` base before the `tempo` cable |
| Everything transposed by octaves | note names on a `freq:` base that isn't 220 Hz |
| Note retunes during its own tail | no `sh` holding pitch after the switch/quantizer |
| Reverb ghosts of a muted part | send tapped pre-mute; tap it from the post-fader `vca` |
| `curve: exp` hit far shorter than its time | the time is the −60 dB point: write ~3x a linear time (patch-craft.md) |
| Intro reappears at the end | the arrangement grid wrapped; render shorter than `steps × block` |
| `quant` output missing | it's `quant.out`; only `stepseq` has `.cv` |
| Voice muted for a whole section | `logic{and}` thresholds at 0.5 and your lane sits below it — compare against a floor with `logic{op:cmp}` instead |
| Bass inaudible, `--stats` looks fine | note name on a `freq: 55hz` base — see rule 2 |
| Mix distorts for no reason | `bass.sub` clips at unity; stage it at 0.4–0.5 |

## Ship it

A patch is done when `check` passes, `--stats` matches the arrangement you
wrote in the header comment, and the header comment describes what actually
happens. Write that comment first — it is the composition brief.
