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

For LUFS and true-peak, that's outside the patch — use `ffmpeg`:

```bash
ffmpeg -i take.wav -af ebur128=peak=true -f null -
```

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
