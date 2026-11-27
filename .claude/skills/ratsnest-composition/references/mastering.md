# Mastering: the chain and the numbers

## The proven chain

```text
master_dc = dcblock { cutoff: 12hz }
master_mix.out_l -> master_dc.in_l
master_mix.out_r -> master_dc.in_r

master_eq = eq { hp_freq: 28, low_freq: 100, low_gain: -0.5,
                 mid1_freq: 380, mid1_gain: -1.2, mid1_q: 0.9,
                 mid2_freq: 3400, mid2_gain: 0.7, mid2_q: 0.8,
                 high_freq: 10000, high_gain: 0.6 }
master_dc.out_l -> master_eq.in_l
master_dc.out_r -> master_eq.in_r

master_comp = compressor { sidechain: external, threshold: -16, ratio: 2.4,
                           knee: 5, attack: 0.012, release: 0.14,
                           makeup: 7.0, mix: 0.85 }
master_eq.out_l -> master_comp.in_l
master_eq.out_r -> master_comp.in_r
kick_drive -> master_comp.side_l : 0.7
kick_drive -> master_comp.side_r : 0.7

master_field = stereo { width: 1.15, balance: 0 }
master_comp.out_l -> master_field.in_l
master_comp.out_r -> master_field.in_r

master_clip = clipper { mode: soft, threshold: -2.5, drive: 1.2, mix: 0.32 }
# drive is in dB. 1.2 is a gentle album setting; 4-6.5 is the loud end of the
# range. Raise it only while watching crest stay above ~10 dB.
master_field.out_l -> master_clip.in_l
master_field.out_r -> master_clip.in_r

master_limit = limiter { threshold: -0.5, release: 0.08 }
master_clip.out_l -> master_limit.in_l
master_clip.out_r -> master_limit.in_r

master_limit.out_l -> out.left
master_limit.out_r -> out.right
```

Order matters: DC first (so nothing downstream sees offset), EQ before dynamics
(so the compressor isn't triggered by mud you were going to cut anyway), width
after compression (so the compressor stays stereo-linked on the real signal),
clipper before limiter (the clipper shaves peaks so the limiter barely works).
For a release master add `true_peak: on` to the limiter: sample-peak limiting
lets dense mixes overshoot by 1.6–2.3 dB between samples (see `limiter` in
`docs/modules.md`).

## The calibration numbers

Working backwards from a loudness target:

| Stage | Target |
|---|---|
| premaster peak | ≈ −3 dBFS |
| glue comp makeup | +6.5 .. +8.5 dB at `mix` 0.75–0.85 (linear blend; measured on mainstage/ion/polaris/halcyon) |
| clipper drive | 1 .. 6.5 dB (it is **dB**), parallel `mix` ~0.3 |
| limiter threshold | −0.4 .. −0.5 dBFS |
| drop-window RMS (club) | ≈ −12 dBFS, peaks −0.4 → crest ~11 dB |
| breakdown | 6–8 dB below the drops |
| intro | ≈ −25 dBFS RMS or lower |

Crest ~11 dB is loud but not bricked. Crest under 6 dB means you flattened it.

The compressor's detector holds each peak for `min(release, 20 ms)`, so a
steady tone gets exactly the static curve at its peak. A slow-attack glue comp
on its own input (`attack: 0.05, release: 0.3`) compresses 2–3 dB harder than it
did before that hold went in; the library's patches were rebalanced with makeup
(+0.5..+3 dB) to keep their loudness. If an older patch comes back quieter,
raise makeup by the difference rather than lowering the threshold. The limiter
holds the same way, which costs a brickwalled master (≈ −8 LUFS) about 0.5 dB;
that is the price of a clean low end, not a reason for more drive.

## The lib chains: `master-chain.sub` and `club-master.sub`

`patches/lib/master-chain.sub` is the chain above as one instance; its
`width` and `makeup` are inputs, so form lanes can ride them. Measured on two
full songs (glasshouse, synth-pop; undertow, melodic techno), four things it
does that you have to plan around:

- **Polite by default.** All defaults on whole-step: −13.5 LUFS integrated;
  the synth-pop mix, staged lower, came back at −16.7. Loudness comes from
  `makeup` and `clip_drive`, about 0.5 LUFS per dB of either near the top.
- **Kick-keyed compression leaves kick-less sections uncompressed — and
  still adds `makeup`.** The key is external (`side_l`/`side_r` ← kick), so a
  breakdown with no kick gets no gain reduction but the full makeup: one
  measured breakdown came back 1 dB under the chorus it was mixed 6 dB
  below. Mix kick-less sections ~6 dB lower than you want them, ride
  `makeup` down with the form lane, or key from the bus too
  (`key_bus: 0.35` adds that share of the bus to the key, so the glue holds
  every section).
- **The clipper overshoots its threshold.** At the default `clip_blend` 0.32
  the clip is parallel: the dry 68 % passes unclipped, so the clipper's
  output overshoots its −2.5 dB threshold by 2.4–3.4 dB. It rounds peaks; the
  limiter sets the ceiling. `clip_blend: 1` makes it a real (serial) clipper.
- **The limiter is sample-peak unless told otherwise.** With the default
  `true_peak: off`, inter-sample peaks exceed `ceiling` by 1.6–2.3 dB on a
  loud master (whole-step: −0.4 dBFS sample peak, +1.7 dBTP). Set
  `true_peak: on`, or keep the ceiling at −2.5 dBFS or lower, for a release.

`patches/lib/club-master.sub` is the club preset: glue at −18 dB 3:1 keyed
by kick plus 35 % bus, serial soft clip at `drive` 12.5 dB, makeup 10.5 dB
(an input), true-peak limiter at −1.0 dBTP. On undertow's bus it measured
**−8.5 LUFS integrated, −1.0 dBFS sample peak, −0.5 dBTP**, drops −7.8 dBFS
RMS (crest ~7 dB: loud, as club masters are). `drive: 13` with `makeup` 11
reaches −8.0. At that density a kick-less break sits only ~2.3 dB under the
drop, so mix breaks lower still.

## When one band misbehaves

The chain above treats the mix as one signal. When only the low end pumps
or only the top is harsh, split it: `crossover` (three Linkwitz-Riley bands
that sum flat) with a `compressor` per band is multiband compression, and a
`follow` on a band-passed key riding an `eq` gain is a dynamic EQ (both are
recipes in `docs/modules.md`). Fix drums before the bus, not on it:
`transient { attack: 4 }` gives a soft kit snap back without raising the
peak-to-loudness ratio the limiter has to absorb for long, and `gate`
cleans room and bleed tails that the glue compressor would otherwise lift.

## The self-own that costs 2 dB

A post-limiter attenuation scales the ceiling. The fade idiom

```text
-1 -> out.level
main_lvl -> out.level : 0.8
```

fades the track, but at full play that `0.8` turns a −0.4 dBFS limiter ceiling
into a −2.3 dBFS master. Your render comes back quiet and no amount of clipper
drive fixes it without crushing.

**Use `: 1.0` and let the limiter own the ceiling:**

```text
-1 -> out.level
main_lvl -> out.level : 1.0
```

## Verifying

```bash
mise run render -- patches/x.patch -d 240s -o /tmp/x.wav --stats 4s
```

Check, in order:

1. **Overall peak** ≤ the limiter threshold. If not, something bypasses the chain.
2. **DC ≈ 0.** If not, find the pulse/fold/abs and block it.
3. **No flat window** where peak == RMS. That's a brick.
4. **Section RMS steps** land at the seconds your block table predicts.
5. **No dead air** after the fade, and no return of the intro (grid wrap).

`render --stats` prints the integrated LUFS, loudness range and true peak
(dBTP) on its first two lines, and LUFS per window in the timeline; it matches
`ffmpeg -af ebur128=peak=true` to 0.1 LU. Calibrate `makeup` against those
numbers instead of rendering twice. For a 16-bit delivery, render with
`--bits 16` (TPDF-dithered) rather than letting an encoder truncate the float.

## When not to master

Ambient, drone and dark ambient masters should keep 18 dB+ of crest and sit at
−20 to −26 dBFS RMS. Run `dcblock -> eq -> limiter` at −1 dBFS and stop. The
compressor and clipper exist to make club tracks compete; a drone competing is a
drone that has lost.

For an infinite scene, do the fade in post rather than in the patch:

```bash
ffmpeg -i take.wav -af "afade=t=in:st=0:d=4,afade=t=out:st=291:d=9" out.wav
ffmpeg -i out.wav -af astats -f null -    # confirm the head and tail
```
