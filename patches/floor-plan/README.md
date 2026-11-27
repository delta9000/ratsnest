# Floor Plan

*Eight rooms of one building.* Every track is native Ratsnest (no plugins),
and every track is set in a different space with its own dynamics. The
earlier albums used the same plate reverb throughout. Here the room is the
subject of each track, built from the effects and mastering tools added in
#25 to #28.

A four-note figure runs through the album: scale degrees 1 5 4 ♭3. Each
track states it in its own key, and the last track plays it in major.

| # | Title | Length | Style, key, tempo | The space | The dynamics | The figure |
|---|---|---|---|---|---|---|
| 1 | Cloakroom | 2:55 | lo-fi boom-bap, F minor, 88 | a tiny hall (`size` .3, ~0.4 s) at a modest send: close and dry | drum bus through `transient` (+4 attack, −6 sustain); biased `saturate` on the master bus | pluck, intro and outro (F C B♭ A♭) |
| 2 | Stairwell | 2:57 | dub techno, C minor, 118 | a ping-pong `echo` (dotted eighth) into a tall bright hall (`size` 1.7, little damping) | echo feedback rides a form lane; echo return band-limited by `eq` `hp_freq`/`lp_freq`; makeup follows the kick lane | a pluck thrown into the echo (C G F E♭) |
| 3 | Cistern | 3:26 | ambient, E♭ minor, 60 | an enormous hall (`size` 4, `decay` .93), frozen for four bars while the dry parts drop out | dynamic EQ on the return (`follow` on 320 Hz rides an `eq` bell, up to ~9 dB); no compressor | modal bell (E♭ B♭ A♭ G♭) |
| 4 | Plant Room | 3:00 | industrial techno, A minor, 132 | a small metal room (`size` .45, no damping) and a kick rumble from a low-passed hall | three-band compression (`crossover` + three compressors) into `saturate`, serial clip, true-peak limit; parallel `saturate` grit on clap and hats; steam through a `gate` keyed by a tracker lane | the second pipe, in the break (A E D C) |
| 5 | Glass Lift | 2:56 | synth-pop, G minor, 108 | gated reverb: a big hall cut off by a `gate` keyed from the dry snare (open ~250 ms, shut by 300 ms); a small bright room for the synths | pad low-passed by an `eq` `lp_freq` form lane; lead echo is a stereo `echo` (dotted eighth left, quarter right) | the chorus hook (G D C B♭) |
| 6 | Atrium | 3:08 | neo-classical, A♭ major, 72 | a tall hall (`size` 2.2) with a placed stage: piano left, cello right, true-stereo reflections | piano through `transient` (−2 attack, +5 sustain) for bloom; `eq` with `low_q` .707 and a `high_q` 2 air bump; 1.6:1 bus compression | the bowed cello (F C B♭ A♭); the piano in the coda with C♭, then C |
| 7 | Corridor | 2:58 | motorik, D dorian, 128 | a hall with a 90 ms predelay (a far wall); a stereo `echo` at 11/17.6 ms as a Haas widener on the plucks; a second one repeating quarter / dotted quarter | bass through an `eq` `low_q` 2 bump and biased `saturate` | a long lead in B (D A G F) |
| 8 | Front Door | 2:37 | closer, D major, 66 | no reverb at all for eight bars, then a small room, then a big hall (`size` 3) that freezes on the last chord as the wind comes in | DC block, EQ, 1.5:1 compression, true-peak limit | track 1's electric piano and pluck, in major (D A G F♯) |

Total 23:58. Files are 320 kbps MP3.

**Bookends.** Track 8 brings back track 1's FM electric piano and its
pluck: the room you started in, heard again on the way out.

## Loudness

Set inside each patch from `render --stats` (no loudness normalisation in
ffmpeg; the encode only trims trailing silence and fades the last 1.5 s).
Measured on the MP3s:

| # | Track | Integrated | LRA | True peak |
|---|---|---|---|---|
| 1 | Cloakroom | −14.0 LUFS | 3.1 LU | −1.3 dBTP |
| 2 | Stairwell | −14.9 | 4.7 | −1.3 |
| 3 | Cistern | −19.1 | 8.1 | −1.5 |
| 4 | Plant Room | −10.2 | 5.3 | −1.6 |
| 5 | Glass Lift | −12.6 | 6.8 | −1.1 |
| 6 | Atrium | −17.1 | 5.7 | −3.1 |
| 7 | Corridor | −12.7 | 4.5 | −0.4 |
| 8 | Front Door | −17.5 | 6.5 | −1.5 |

The spread is deliberate: Plant Room is the loud one, Cistern and the two
acoustic pieces keep their crest. The beat tracks limit at −1.8 dBTP so the
MP3 encode stays under 0 dBTP.

## How it was checked

There was no listening model available (the API keys in the cloud
environment have expired), so every judgment comes from measurement:

- **Balance.** Every part was tapped with `--tap` and rebalanced per section
  until no one part buried the others.
- **Bands.** Four-band energy per section, compared with *Ghost Notes*; low
  end L/R correlation checked where reverbs return lows.
- **The effects themselves.** The Glass Lift gate was measured on the hall
  return (open at 250 ms, 60 dB down by 300 ms). The Cistern dynamic EQ's
  detector was tapped to set its depth. The Plant Room band compressors'
  gain reduction was tapped and the thresholds raised until the top band
  averaged a few dB, not ten.
- **Section loudness.** Kick-less sections (the dub break, the bridge)
  came back louder than the drops through the master's makeup, so the
  makeup now follows the drum lane on those tracks.

Things the measurements caught on the way (none an engine bug): a string
fed bowed noise is about 30 dB hotter than its plucked level; `strip.sub`'s
`width` above 1 removes mid, so it drains a near-mono source such as the
piano soundboard; and `modal` drips are bright enough to need a low-pass.

## Files

- `NN Title.mp3`: the eight tracks.
- `patches/`: the `.patch` and `.trk` files, the same as `patches/floor-plan/`
  in the repository.

## Re-rendering

```sh
ratsnest render patches/floor-plan/cloakroom.patch -d 180s -o cloakroom.wav --bits 24 --stats 30s
```

Render lengths in seconds: 180, 184, 214, 185, 181, 188, 183, 163. All parts
are seeded, so renders repeat exactly.
