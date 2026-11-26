# Ghost Notes

An album of eight tracks: Surge XT (loaded headless through `plugin`) next
to ratsnest's own voices, with every part moved per step by the tracker or
by slow LFOs. Rendering the Surge parts needs Surge XT's CLAP installed
(`docs/plugin.md`); without it they are silent and everything else plays.

This follows *Headless*, whose tracks were static: one loop per part, Surge
velocity flat, little width. Here every track uses the things that fixed
that in `patches/riffs/`:

- **Per-step locks.** Filter cutoff and resonance, FM depth, wrap and mask,
  modal material, speech segment, bow pressure and chord quality (Osc 2/3
  pitch) are tracker columns, so neighbouring notes differ in timbre.
- **Chance and late hits** (`?P`, `@`) on ghost notes, re-hits and
  re-voiced chords, so no two passes of a pattern play the same.
- **Free-running loops** of 5, 6, 7, 9, 11, 12 and 13 rows against the
  16-row bar (`name~`), so the layers line up differently each bar.
- **Movement over time.** Each track's `-form.trk` moves levels and filter
  centres per bar, and LFOs at unrelated rates move Morph, Width, density
  and pan.
- **Accents.** Each Surge part sets `A Velocity > VCA Gain=-48 dB` and takes
  vel × 0.2.
- **Width** from unison, Ensemble/Chorus/Rotary, panned loops and stereo
  returns, with the lows kept mono.

| # | Patch | Style | Surge | Ratsnest |
|---|---|---|---|---|
| 1 | `paper-lanterns` | lo-fi, 86 BPM, C minor | Twist kit (engine per row) through Tape; wavetable pad (chord in one note, tape degrade lane); Classic bass with slides; FM2 tine | physical piano, rolled 4-note voicings; kick under the Twist kick; rim/shaker on 7/5-row loops; vinyl |
| 2 | `switchyard` | minimal techno, 124 | acid bass (cutoff + resonance per step); Twist hats/clap/noise; wavetable stabs; FM2 bleeps into Nimbus (5 rows) | kick, clap, open hat; 6-op FM bleeps (7 rows); dub tape echo; riser |
| 3 | `tin-orchestra` | gamelan-shaped, 76 → 108 → 56 BPM | two Twist Modal Resonators interlocking the kotekan (material per stroke); Twist 2-op FM kenong | modal saron (balungan); FM gong; drone; kick and 7-row shaker; the form drives the tempo |
| 4 | `murmuration` | ambient, 72, A♭ lydian | Window pad (3 Morph LFOs); String on an 11-row loop; Twist Particle Noise flock | modal bars on 7/13 rows; `grains` on the strings; sympathetic drone + sine sub; FM bell |
| 5 | `concrete-garden` | broken beat, 96, D dorian | Alias lead (wrap/mask per note, bit depth by form); Twist Vowels/Speech choir (segment per note) | physical kit with flams/ratchets; 9-row toms; 6-op FM electric piano; reese |
| 6 | `glasshouse` | 2-step garage, 132 swung, G minor | FM2 organ bass (bark per note); Classic pulse stabs; Twist speech chopped in 16ths | kit (snare + clap), 6-row open hat, sweep |
| 7 | `night-ferry` | liquid drum and bass, 172, F♯ minor | supersaw pad; wavetable reese (cutoff per note + bar wobble); Sine lead with delayed vibrato | physical kit two-step with fills; 6-row ride; physical piano; rain |
| 8 | `lantern-out` | closer, 66, C major | Sine ×3 organ through Rotary (horn speeds up in B); bowed String cello (pressure per bow); track 1's FM2 tine | physical piano (melody + chords, pedal per change); brushes on 12 rows, rim on 5; track 1's crackle |

A four-note figure (scale degrees 5 ♭3 4 2) runs through the album: track 1's
tine opens with it (G E♭ F D), and track 8 closes with it in major (G E F D)
on the same tine.

```sh
ratsnest render patches/ghost-notes/paper-lanterns.patch -d 184s -o paper-lanterns.wav
```

Render lengths in track order: 184, 176, 230, 182, 185, 180, 184, 182 s,
then trim the silent tail. Tin Orchestra's drone and hall keep ringing after
the last gong; the album version fades from 3:04 to 3:18.
