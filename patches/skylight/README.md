# skylight — a slushwave album

Six tracks, about 18–20 minutes: the last day of Skylight Galleria, a mall
closing for good, from opening hour to the night the atrium floods.

The device that holds it together: **it is all one tape, slowing down.** Each
track is the same VHS played a little slower and a little more worn than the
last, so tempo and pitch fall together (pitch = `log2(bpm / 96)` octaves,
exactly what slowing a tape does) and the wear rises. One melody, the mall's
**jingle**, is stated clean in track 1, comes back in fragments, and returns
whole and underwater in track 6 before the tape winds down to a stop.

| # | File | Story | BPM | Tape speed (oct) | Sounds in | Wear |
|---|---|---|---|---|---|---|
| 1 | `1-opening-hour` | 10 a.m.: doors open, the PA welcome, muzak in the fountain court; the jingle stated clean | 96 | 0 | G major | almost new |
| 2 | `2-food-court` | lunch rush; a smooth-funk groove, kids on Saturday | 88 | −0.125531 | ~F | light |
| 3 | `3-atrium` | afternoon: rain gets in through the skylight | 72 | −0.415037 (written in D) | D major | medium |
| 4 | `4-escalator` | closing time; stores go dark one by one; the escalator rises forever | 64 | −0.584963 | ~C (minor turn) | heavy |
| 5 | `5-security-office` | beatless interlude: the guard's night, the flood watch, someone finds the tape | free (60) | −0.678072 | ~Bb | heavier |
| 6 | `6-flooded` | the atrium floods; the jingle returns underwater; the tape winds down to a stop | 56 → 0 | −0.777608 → down | ~A | drowned |

## Shared DNA

- **Write every track in G** (tracks 1, 2, 4, 5, 6) and add the track's tape
  speed as a constant on its `wow` pitch bus, which reaches every pitched
  input (`fm`/`synth-voice` `v_oct`, `sampler.pitch`, `grains.pitch`). That
  is the whole transposition: the tracker files read as G throughout the
  album. `3-atrium` predates this and is written directly in D (its speed,
  −4.98 semitones, is within 2 cents of D).
- **The wow bus** (from `3-atrium`): `lfo` sine ~0.5 Hz + `random` drift
  summed in a `math { op: add }`, plus the speed constant, plus the track's
  tape-stop lane where it has one. Depth rises with wear (see below).
- **Voices of the band**: the FM e-piano (`3-atrium`'s `keys.ep`, a `chord`
  bank stamping one `fm` per note), `lib/synth-voice.sub` bass, the physical
  kit (`lib/kit/*.sub`), the trumpet lead from two CC0 samples split at A4
  (`3-atrium`'s `tp_lo`/`tp_hi` block, with its `sh` pitch holds), grain
  hazes from `patches/samples/cc0/`. Each track adds its own identity on top
  ("Each track's identity", below); do not make six copies of atrium.
- **Space**: a long `reverb` plate as the room, a dotted-eighth `delay` off a
  `tempo` module; post-fader sends from `lib/strip.sub`.
- **Master**: `lib/master-chain.sub`, `ceiling: -1, true_peak: on`, a high
  shelf that darkens as wear rises, `main` lane into a pair of fade VCAs.
  Album loudness: **−16 LUFS integrated per song, −19 for the interlude**
  (measured with ffmpeg `ebur128=peak=true`), `clip_drive: 1`, makeup set
  per track to land there; tails fade to silence before the render ends.
  (First pass mastered at −12.8..−13.7 LUFS with 3.6–5.8 LU range: too hot
  for the genre.)
- **The tape itself**: `vhs.sub` in this folder (wow/flutter, hum,
  overdrive, VHS band, dropouts, hiss). Everything "recorded" goes through
  one: the rain, the voices, and from track 2 on a growing share of the
  music bus (a parallel send that rises with wear).

## Each track's identity

The tracks share the tape and the jingle, not the arrangement. Each one has
its own loop, groove, lead and one signature device; a track that measures
like `3-atrium` with different notes has failed its brief. Written in G
(the speed constant does the rest).

| # | Chord loop | Drums | Lead / colour | Signature | Form, length |
|---|---|---|---|---|---|
| 1 | `Gmaj9 \| Em9 \| Cmaj9 \| D6/9`, one bar each | tight, bright, almost four-on-the-floor light boogie; closed hats on 8ths | bright FM e-piano (cut open), FM bells/`lib/mallet.sub`, trumpet states the jingle | the PA chime (D5–B4 on bells) and `pa-open` over the **fountain** (the rain loop through `grains`, pitched up ~+0.6 oct, bright, quiet) | chime/PA intro, groove, jingle, groove, jingle, outro; ~3:00 |
| 2 | `Am9 \| D9 \| Gmaj7 \| Em7` (ii–V–I–vi) | swung 16ths (`transport { swing: 0.16 }`), syncopated kick, rim/clap on 2 and 4 | driven slap-ish bass (synth-voice pluck), offbeat clav-like plucks, a **chopped vocal riff**: `vox-vibrato` through `grains` with `density: 0`, one grain per tracker trig, pitches from a pattern | the vocal chops; the bass quotes the jingle's D–B–G | groove-led, a breakdown on the chops; ~3:20 |
| 3 | (as built: `Gmaj9 \| F#m7 \| Em9 \| A7sus4` in D) | half-time, soft, mostly room | trumpet, e-piano, haze | the rain through the skylight; tape stop | ~3:33 |
| 4 | `Em9 \| Cmaj7 \| Am9 \| Bm7` (the minor turn) | sparse half-time; rimshot instead of snare; thins out | an endless **Shepard-tone escalator** (octave-spaced partials, bell-shaped level over log pitch, rising continuously); the jingle's first bar on trumpet at half speed | the stores go dark: in the second half the form lanes switch parts off one by one, every 4 bars, until only the escalator, rain and the room are left | build, full, then the lights-out subtraction; ~3:30 |
| 5 | no loop: a frozen-reverb drone on `Em9`, moving to `Cmaj9` | none | the found tape: the jingle once, as if from track 1, through a hard `vhs.sub` (drive .7, hi 3500); **rewind/fast-forward scrubs** (a sampler/grains pitched +2 oct and swept) and tape stop/starts; rain outside, low-passed ~800 Hz | the voices carry it: `pa-security` as a walkie-talkie (band ~400 Hz..3 kHz, a little drive), `iv-guard` and `wx-flood` from a TV, `pa-closed` | a scene, not a song; ~2:00 at a fixed 60 BPM form clock |
| 6 | `Gmaj9 \| Em9 \| Cmaj9 \| D6/9` (track 1's loop, now drowned) | a muffled kick like a heartbeat on 1 (and the "and" of 2 sometimes) | the **full jingle reprise** on trumpet under an underwater low-pass (~500..1500 Hz, swept slowly), big plate with freeze moments, rain loud and close | the **wind-down**: over the last ~8 bars a lane ramps `transport.bpm` toward ~8 BPM and the speed bus down ~2 octaves together (the tape dying); the form is clocked by its own fixed `clock`, not the slowing transport, so the fade rows still arrive; then hiss, then silence | reprise, flood, wind-down; ~4:00 |

## Consequences of the speed offset (apply them)

- **Bass register.** A written G1 (49 Hz) sounds ~33 Hz in track 4 and
  ~29 Hz in track 6: the "inaudible bass, stats look fine" bug. Tracks 4–6
  write the bass an octave up (G2); confirm with the band check
  (`lowpass=f=120` vs the mids).
- **Voice clips stretch** by `2^(-speed)`: `pa-last` (10.2 s) runs ~17.5 s
  in track 6. Hold each voice gate for its slowed length in bars.
- **Haze tunings in G notation** (before the speed constant): `vox-vibrato`
  (C#4 −18 c, 0.3183 v/oct — but only at grain positions ~0.07–0.24; the
  phrase slides down to ~F#3 by position 0.8, so pitched chops must stay in
  that range, while a haze that scans wider glides over a fourth) to D4 = `+0.0983`, to B3 = `−0.1517`, to G3 =
  `−0.4850`; `piano-c-progression` (C major) to G = `−0.4167` (or
  `−1.4167` an octave darker). The trumpets: written note minus `0.6658`
  (F4 file) or `1.4200` (D5 file), split at A4, exactly as in `3-atrium`.
- **Delays and LFO rates** follow the track's own BPM (`tempo` module);
  nothing else needs the speed.

## Wear, track by track

| # | `wow` lfo / drift depth (oct) | vhs.sub on | vhs.sub settings | master `high_gain` |
|---|---|---|---|---|
| 1 | 0.0015 / 0.002 | voices only | `drive: 0.2, hi: 7500, drop_rate: 0.02hz` | 0 |
| 2 | 0.0025 / 0.004 | voices + ~20% music | `drive: 0.35, hi: 6500, drop_rate: 0.06hz` | −2 |
| 3 | 0.004 / 0.006 | rain (as built) | defaults | −5 |
| 4 | 0.006 / 0.008 | voices, rain + ~45% music | `drive: 0.55, hi: 4500, drop_rate: 0.18hz` | −7 |
| 5 | 0.008 / 0.010 | everything | `drive: 0.7, hi: 3500, drop_rate: 0.3hz` | −8 |
| 6 | 0.010 / 0.012 | everything, plus an underwater low-pass (~500..1500 Hz, swept) | `drive: 0.6, hi: 2800, drop_rate: 0.25hz` | −9 |

## The jingle

The mall's jingle, in G major pentatonic, two bars of sixteenths:

```text
pattern jingle 2x16 lead
1:00 | D5:4  .8
1:04 | B4:2  .7
1:06 | G4:2  .65
1:08 | A4:6  .75
1:14 | B4:2  .6
2:00 | D5:6  .85
2:06 | E5:2  .7
2:08 | D5:8  .75
```

Over `Gmaj9 | Em9` (or `Cmaj9 | D6/9` as the answer). The PA chime that
opens track 1 is its first two notes, D5–B4, on bells. Fragments: track 2
quotes bars 1's first three notes in the bass/keys; track 4 slows the first
bar to half speed; track 5 plays it once, through the tape, as the found
recording; track 6 is the full reprise.

## Voices (`patches/samples/skylight/`, original scripts, local TTS)

| Track | Clips |
|---|---|
| 1 | `pa-open`, `iv-walker` |
| 2 | `pa-foodcourt`, `iv-teen` |
| 3 | `wx-local` (bar 2), `wx-current` (break), from a TV through `vhs.sub` |
| 4 | `pa-closing`, `iv-escalator`, `iv-owner` |
| 5 | `pa-security`, `iv-guard`, `wx-flood`, `pa-closed` |
| 6 | `pa-last`, `iv-mom` |

Play a clip with a `sampler` (no loop) triggered by a gate column in the
form tracker on the bar it should start, its `pitch` taking the track's tape
speed (so the voices slow with the tape: normal in track 1, screwed by track
6). PA voices: band-limit ~300 Hz..4 kHz and send heavily to the room (a PA
in a glass atrium). Interviews and weather: through `vhs.sub`, drier, as if
from a TV in another room. Duck the music 2–3 dB under a voice.

**Gotcha (measured):** a `sampler` plays its file once at load *even with
`trig` patched* — a trig that never fires still gives the clip at t = 0. So
every voice sits behind a `vca { gain: 0 }` opened by an `adsr` on the same
form gate, held for the clip's length (a note length on the form cell, e.g.
`x:3` for three bars), release ~0.5 s. The trumpet block in `3-atrium` is
already built this way.

Paths: `sampler`/`grains` files resolve from the **repo root**
(`"patches/samples/skylight/pa-open.wav"`); `sub`/`tracker` files resolve
from the patch's folder (`"../lib/strip.sub"`, `"vhs.sub"`).

## As built (2026-11-20)

All six tracks pass `check`; renders and measurements below are from one
pass with the final files. Render lengths: 176, 198, 216, 217.5, 124 and
232 s (`-d` that long; each ends in silence or tape hiss under −69 dB).

| # | BPM | speed | wow bus | vhs.sub on | master `high_gain` | mix RMS before the loudness pass (mono) | energy >6 kHz |
|---|---|---|---|---|---|---|---|
| 1 | 96 | 0 | 0.0015 / 0.002 | voices (drive .2, hi 7500) | 0 | −13.9 | −16.9 dB |
| 2 | 88, swing .16 | −0.125531 | 0.0025 / 0.004 | voices + parallel music return (drive .35, hi 6500) | −2 | −13.9 | −21.3 dB |
| 3 | 72 | (written in D) | 0.004 / 0.006 | rain (inline chain = `vhs.sub` defaults, bit-identical), TV weather | −5 | −14.6 | −42.8 dB |
| 4 | 64 | −0.584963 | 0.006 / 0.008 | voices, rain, music send (drive .55, hi 4500) | −7 | −14.3 | −54.4 dB |
| 5 | 60 (scene clock) | −0.678072 | 0.008 / 0.010 | VCR, TV, bed (drive .7, hi 3500) | −8 | −17.5 | −50.0 dB |
| 6 | 56 → 8 | −0.777608 → −2.807 more | 0.010 / 0.012 | voices, both music channels (drive .6, hi 2800, `lo: 40`) | −9 | −15.0 | −53.8 dB |

A chroma key-profile fit over each track's middle 60% reads (corrected for
its consistent fifth-up bias on these maj9/6-9 voicings) G, F#, D, C, B, Bb:
the tape speeds' G, ~F/F#, D, C, ~B, ~A/Bb.

Deviations from the brief, kept on purpose:

- **Trumpet offsets** are `0.6658` / `1.4200` (re-measured to ~1 c); the first
  published `0.6608` / `1.4117` played 6–10 c sharp. All patches use the new values.
- **`vox-vibrato` slides** C#4 → ~F#3 over the file; track 2's pitched chops
  read positions 0.15–0.19 only. The hazes in 3 and 4 scan wider and glide
  between chord tones (D/A in 3, B/E in 4).
- **Track 1**: a soft `clipper` (−8) on the drum bus (the kit kick's ~26 dB
  crest peaked +9.7 at the needed gain); voices are fed ×4 into a low-drive
  `vhs.sub` (drive .2 is ~12 dB quieter than the default).
- **Track 4**: lights go out with 0.25 s falls and a thunk on bars 32, 36,
  40, 44, 48; `4-escalator-shepard.sub` is the escalator.
- **Track 5**: an end-of-gate trigger from `logic { op: not }` fires at load
  (not of a low gate rises at t = 0); the walkie squelch is latched with `sh`.
  It carries almost no energy below 120 Hz (beatless, everything on tape).
- **Track 6**: the full-mix tape lost the bass (150 Hz band high-pass plus
  the overdrive's mid hump), so its music tapes use `lo: 40` and a parallel
  path below 140 Hz carries bass and kick around the tape. The wind-down goes
  56 → 8 BPM and the full matching −2.807 octaves; the music fades out from
  201 s, so about two octaves of it are heard.

Loudness pass (same day): makeup 3.5 / 6 / 1.5 / 1.5 / 1.6 / 0.7 dB,
`clip_drive: 1` on every master. Integrated −15.9, −16.0, −16.1, −16.2,
−19.0, −16.0 LUFS; true peaks −0.9, −1.0, −1.0, −3.4, −5.2, −5.0 dBFS; the
album file −16.2 LUFS, 8.1 LU range.
