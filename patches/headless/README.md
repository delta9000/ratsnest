# Headless

An album of eight tracks, each built around one Surge XT oscillator, hosted
headless by the `plugin` module (docs/plugin.md). Every Surge sound is a
`set:` list in its patch: no preset files, no plugin state. The notes are in
the `.trk` files, and each track's levels are in its `-form.trk`. Without
Surge XT installed, the patches still check and play, with the Surge parts
silent.

| # | Patch | Surge oscillator | Render |
|---|---|---|---|
| 1 | `init-saw` | Classic, and the bare init patch | `-d 175s` |
| 2 | `wavetable-weather` | Wavetable, Sine | `-d 202s` |
| 3 | `fm2-for-hammers` | FM2 (with the physical piano) | `-d 192s` |
| 4 | `two-strings-stiff` | String | `-d 185s` |
| 5 | `twist` | Twist (Waveshaper and Vowels/Speech engines) | `-d 185s` |
| 6 | `alias` | Alias | `-d 178s` |
| 7 | `window` | Window, with the Nimbus effect | `-d 192s` |
| 8 | `sine-finally` | Sine (with the physical piano) | `-d 145s` |

A four-note motif, scale degrees 1 b3 2 5 (D F E A), appears in every
track in that track's key.
