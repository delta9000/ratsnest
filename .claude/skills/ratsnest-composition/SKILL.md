---
name: ratsnest-composition
description: Use when a ratsnest patch needs to become a piece of music rather than a loop — deciding form, harmony, arrangement, section transitions, frequency balance, sidechain, stereo placement, or a mastering chain, or when a render sounds flat, muddy, quiet, or structurally shapeless.
---

# Composing in ratsnest

A loop becomes a composition when something is *different* at 2:00 than at
0:20, and the difference was decided rather than drifted into. This skill is
about deciding.

Load `ratsnest-patching` first for the authoring loop and the module reference.

## The four questions

Answer these in the patch header comment before writing a cable. The comment
is the brief; the patch is its realization.

1. **How long, and what is the unit?** Bars, blocks, seconds. Write the block
   table (see `references/form.md`).
2. **What is the harmony?** Even if it's one chord for nine minutes, say so.
   See `references/harmony.md`.
3. **Which element carries which frequency slot, and at what level?** See
   `references/mixing.md` and `references/levels.md`.
4. **How loud, and how does it end?** See `references/mastering.md`.

## Two structural strategies

| | Tracker grid | Infinite scene |
|---|---|---|
| Use for | songs with drops, builds, an ending | drones, generative beds, DJ tools |
| Time base | bar-clocked `stepseq` lanes → `func` → `vca.cv` | incommensurate glacial `lfo`s |
| Change comes from | authored lane values | period ratios that never realign |
| Ending | a fade lane on `out.level` | in post (`ffmpeg afade`) |
| Failure mode | grid wraps and the intro returns | nothing ever *happens* |

Both are legitimate. Most good tracks in this repo are the grid; most good
drones are the scene. A hybrid — grid for the drums, scene for the pads — is
usually better than either.

## The moves that read as "composed"

- **Re-voice from three numbers.** Root/third/fifth chord lanes feed bass, pad,
  arp and lead. Change the arrays, the whole track re-harmonizes.
- **Make one parameter the protagonist.** A filter cutoff that opens over four
  minutes, a `euclid.hits` that thickens, a reverb decay that swallows the
  piece. Name it in the comment. Everything else supports it.
- **Subtract for the drop.** A breakdown is not "quieter"; it is *fewer
  elements, wider*. Mute the kick, open the reverb sends, raise the pad.
- **Asymmetric envelopes.** Risers rise for 6–15 s and fall in 0.4 s. That
  gap is the drop.
- **Leave one thing unresolved.** A cadence that never lands (i–v–VI–VII) is
  why peak-time loops loop.

## Deliberate imperfection

Grid-perfect is a choice, not a default. Detune by 1 Hz rather than 0 cents.
Put a `bern { prob: 0.85 }` on the hats so one in seven drops. Run a slow
`random` into pitch at ±0.004 v/oct. Let two delays sit at 3/8 and 1/4 so the
echoes tangle. None of this is audible as an effect; all of it is audible as
life.

## References

| File | Covers |
|---|---|
| `references/form.md` | block tables, phrasing, transitions, the arrangement grid recipe |
| `references/harmony.md` | chord lanes, modes, quantizer scales, tuning, voice leading, avoid notes |
| `references/mixing.md` | frequency slots, gain staging, sidechain layers, stereo by frequency |
| `references/mastering.md` | the proven chain, the calibration numbers, loudness targets |
| `references/levels.md` | **measured** module output levels, processor gain changes, the summing arithmetic |

## Common mistakes

| Symptom | Cause |
|---|---|
| Sounds like a loop, not a track | no lane changes anything above the bar level |
| Breakdown feels limp | you lowered levels instead of removing elements |
| Muddy | two elements in the same slot at the same time; nothing high-passed |
| Pump sounds like a demo | one duck layer instead of three (channel, FX return, bus) |
| Quiet master | `out.level` scaled below 1 after the limiter |
| Ends abruptly, or the intro comes back | render duration vs. grid wrap not computed |
