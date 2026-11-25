# Riffs: Surge XT, played by the tracker

Two pieces where the tracker moves Surge per step, and one A/B. Rendering
the Surge patches needs Surge XT's CLAP installed (`docs/plugin.md`).

| Patch | What the tracker does to Surge |
|---|---|
| `locks.patch` | **Per-step locks.** The bass's cutoff and resonance are `cva`/`cvb` columns, so every step of the acid line has its own filter. One pad instance plays whole chords: Osc 2 and Osc 3 pitch are columns, so the chord quality (minor, major, sus4, an inversion) is locked per chord. The pluck's echo send is a `cvd` column (dub throws). Ghost hats, rims and shakers are chance and late hits (`?P`, `@`). 124 BPM, F# minor, 72 bars. |
| `locks-native.patch` | **The A/B.** The same two `.trk` files, drums, space and master, with every Surge voice replaced by ratsnest modules that read the columns in Surge's units. |
| `engine-room.patch` | **One Twist is the whole kit.** Each row picks Twist's engine (analog kick, snare, hi-hat, filtered noise; the kick pitched up is the toms), with its first knob and decay. A second Twist is a ghost kit on 6- and 10-row free loops. Twist's chord engine has its chord type and inversion locked per chord, and an inharmonic-string lead has its inharmonicity locked per note. 98 BPM, D minor, 56 bars. |

Things found on the way, all in `docs/plugin.md`:

- Surge's amp ignores velocity above about 0.2, which is why the accents on
  the *Headless* album did nothing. These patches set
  `A Velocity > VCA Gain=-48 dB` and scale velocity by 0.2 (about 8 dB of
  accent).
- Twist's params are renamed per engine, so bind them by id
  (`params: ["627352114"]` is the engine).
- Twist's modal-resonator and 2-op FM engines come out about 15 to 20 dB
  hotter than its drum engines, which is more than velocity can make up, so
  they stay off the main kit.

```sh
ratsnest render patches/riffs/locks.patch -d 143s -o locks.wav
ratsnest render patches/riffs/locks-native.patch -d 143s -o locks-native.wav
ratsnest render patches/riffs/engine-room.patch -d 141s -o engine-room.wav
```
