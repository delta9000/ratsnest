# CC0 sample library

The WAVs are not in git (`*.wav` is ignored). Fetch each source below, convert it
as described, and check it against the converted sha256.

Samples anyone may use, adapt and redistribute with no conditions: every file
here was released under CC0 1.0 (https://creativecommons.org/publicdomain/zero/1.0/)
by its author, verified on the source page's licence box (`{{self|cc-zero}}`,
or `{{cc-zero}}` plus the original's CC0 page for a transfer) on 2026-11-20. Only CC0 goes in this folder; public-domain-release, CC-BY and
anything weaker stay out.

Processing for every file: decoded with ffmpeg and converted to mono 48 kHz
32-bit float WAV (`ffmpeg -i src -ac 1 -ar 48000 -c:a pcm_f32le out.wav`).
No normalising, EQ or pitch change; only `rain-loop.wav` is cut (below).
The instrument and voice sources peak at 0 dBFS (likely clipped), so those
files read about +2.5 dBFS between samples: stage them down at the cable.

Pitches were measured locally (zero-padded FFT, harmonics 1–6 with parabolic
peak interpolation, median over 11 windows of the sustain; spread ≤ 6 c), not
taken from the labels alone. `sampler` plays a file at its own pitch at
`pitch: 0`, so to play note N cable `N - <file's v/oct>` into `pitch`.

| File | Source page | Author | Source sha256 | Measured pitch | v/oct (A3 = 0) |
|---|---|---|---|---|---|
| `trumpet-f4.wav` (3.31 s) | [File:03._F4-trumpet.ogg](https://commons.wikimedia.org/wiki/File:03._F4-trumpet.ogg) | ПростоУчастник | `d59fe3a67cafcef6c6ef6f4d7edbb1ae944cb9622677ec7e7a4d0ecd59179bb7` | F4 −1 c (349.01 Hz), concert | 0.6658 |
| `trumpet-d5.wav` (3.23 s) | [File:05._D5-trumpet.ogg](https://commons.wikimedia.org/wiki/File:05._D5-trumpet.ogg) | ПростоУчастник | `f66cd6ae52e1dcd68c473cc2447a5ae86982e8774a0e47c0892e82b3add12b4d` | D5 +4 c (588.68 Hz), concert | 1.4200 |
| `vox-vibrato.wav` (9.49 s) | [File:Female-vibrato-1.wav](https://commons.wikimedia.org/wiki/File:Female-vibrato-1.wav) | Beetricks | `b023f0094e62f8c3b7c94fd649261b454923e030a0beb42ca32eaf31f2b61d10` | a sung phrase, not a held note: C#4 −18 c (274.3 Hz) with wide vibrato for the first ~25% of the file, then sliding down to about F#3 by ~80% | 0.3183 (first quarter only) |
| `piano-c-progression.wav` (4.90 s) | [File:C_major,_Middle_C,_I-IV-V-I_chord_progression.ogg](https://commons.wikimedia.org/wiki/File:C_major,_Middle_C,_I-IV-V-I_chord_progression.ogg) | CuriousC3301 | `899c21162309220a1379cdb68c5131ab4d5b5fc95a8637aa422c02d4e76d5007` | C major I–IV–V–I around middle C | — |
| `rain-loop.wav` (20.00 s, loops) | [File:Light Rain Distant Thunder July 5th 2016.wav](https://commons.wikimedia.org/wiki/File:Light_Rain_Distant_Thunder_July_5th_2016.wav), mirrored from [freesound 349454](https://freesound.org/people/kvgarlic/sounds/349454/) (CC0 on both) | kvgarlic | `aa85a4aa26b261b8d8d9da52df3cb1c7f09aa86f5253e835ff5e15513e2451cb` | light rain in a forest (Marantz PMD661, two ME66s); unpitched | — |

Converted-file sha256 (check a rebuilt file against these):

    f691657839e1f993458027fb783af89e7f34bf7119475cd96607e04a312e8e86  trumpet-f4.wav
    817e382bea386bb70e861675bcb2fc3c56575af10a3b55c6f737636948c60a4a  trumpet-d5.wav
    69ab5edc120c3058dde29682c6147b1faa16a1864149e4fda24b871f7a383ce6  vox-vibrato.wav
    18043116604d8e8a7f31e36953f2d9e2f563ee3a105c720fe6df1af6b4721300  piano-c-progression.wav
    d76d85b4856b767e7ef317e65fec18937e6160b9546cfe23b6bec891ac9ed852  rain-loop.wav

`rain-loop.wav` is the steadiest stretch of the 110 s source (85..107 s, no
drop on the windscreen above -14 dBFS): the two channels averaged to mono,
then 20 s with the 2 s after it crossfaded equal-power into its head, so
`sampler { loop: on }` wraps without a seam (step across the wrap 0.0021,
median sample step 0.0028). It reads -37 dB RMS: stage it up at the cable.

The direct file URLs are `https://upload.wikimedia.org/wikipedia/commons/8/88/03._F4-trumpet.ogg`,
`.../a/a7/05._D5-trumpet.ogg`, `.../c/c3/Female-vibrato-1.wav` and
`.../5/5a/C_major%2C_Middle_C%2C_I-IV-V-I_chord_progression.ogg`. The piano page
does not say whether the piano is acoustic or rendered.

More verified CC0 candidates not yet downloaded: the same author's trumpet
notes Bb3, Bb4, F5, Ab5 and Bb5 (`File:0N._<note>-trumpet.ogg` on Commons), and
`415061 gsb1039 clock-chime-tubebells-handbells-vibes.wav` (tubular bells at
B2; also CC0 on freesound.org/people/gsb1039/sounds/415061/).
