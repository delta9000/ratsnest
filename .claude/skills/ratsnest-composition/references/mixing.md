# Mixing: slots, staging, ducking, space

You have no ears here. Mixing in ratsnest is arithmetic plus a discipline about
who owns which frequency and who owns which moment.

## Frequency slots

Decide who owns what *before* patching. Two elements in the same slot at the
same time is the only cause of "muddy".

| Slot | Hz | Typical owner | Rule |
|---|---|---|---|
| sub | 30–100 | kick fundamental, sine sub | mono, one element at a time |
| low | 100–250 | bass body, kick knock | high-pass everything else above this |
| low-mid | 250–700 | pad warmth, snare body, reese character | the mud zone; cut here first |
| mid | 700–2k | lead, chord stab, vocal-range interest | the ear's centre; earn your place |
| high-mid | 2k–6k | hat attack, pluck transient, presence | too much = fatigue |
| air | 6k+ | shimmer, cymbal wash, reverb tail | widest, cheapest, safest to add |

Enforce with `svf.hp` / `eq.hp_freq` on every element that doesn't own the low
end. A pad high-passed at 200 Hz sounds thinner soloed and better in the mix —
that trade is the whole job.

**Carve where the other thing lives.** If the pad fights the lead, drop a
narrow `eq` cut in the pad at the lead's centre, not a broad shelf.

## Gain staging

- Voices leave their `vca` around ±0.5 to ±0.8. Anything at ±1 out of a voice
  will clip once four of them sum.
- Set the kick's channel level first (~0.85 with the kick saturated to ~±1).
  It is the peak anchor; everything else stages under it.
- Premaster peak should land ≈ −3 dBFS before the mastering chain.
- `mix` `lvlN` CVs are **additive** to the `levels:` config. A lane fade and a
  kick duck on the same channel simply sum.
- `drive { amount: 0.1 }` is already a ~2× boost into the curve. On a full mix
  that's a limiter, not glue. Use `overdrive { model: klon, mix: 0.5 }` or the
  master `clipper` instead.

## Sidechain ducking — three layers

One layer sounds like a demo. The pump lives at three depths:

```text
# 1. per-channel: the parts that fight the kick
kick_env -> band.lvl2 : -0.24        # bass
kick_env -> band.lvl6 : -0.14        # pad

# 2. FX returns: reverb and delay tails duck too, or the space fills the gap
kick_env -> master_mix.lvl5 : -0.14  # delay L
kick_env -> master_mix.lvl6 : -0.14  # delay R

# 3. bus glue with an external sidechain
glue = compressor { sidechain: external, threshold: -16, ratio: 2.4,
                    knee: 5, attack: 0.012, release: 0.14, makeup: 1.5, mix: 0.85 }
kick_drive -> glue.side_l : 0.7
kick_drive -> glue.side_r : 0.7
```

The duck envelope is a `func { rise: 0.004, fall: 0.28, curve: exp }` on the
quarter clock — not the kick audio. That gives you a clean, adjustable shape
and no bleed. If the arrangement drops the kick, gate the duck envelope with
the kick's arrangement lane or the breakdown will pump against nothing.

Alternatively derive it honestly from audio:

```text
f = follow { mode: peak, attack: 5ms, release: 120ms }
kick_bus -> f.in
f.env -> band.lvl2 : -0.4
```

## Stereo

Width by frequency, low to high:

- **Below ~150 Hz: mono.** One bass, centred, no chorus, no detune-panning.
  Sum a stereo bass with `stereo { mono: 1 }` if you must have one.
- **150–700 Hz: narrow.** Pan ±0.3 at most.
- **Mids: chorus and unison.** `chorus { voices: 3 }` fed from a mono source
  into both inputs fans it into stereo.
- **Air: widest.** Reverb returns hard-panned, shimmer at ±0.85.

Techniques that work:

- **Pan by role, not by taste.** Kick/bass/lead centre; hats and percussion
  ±0.3; pads ±0.5; FX returns ±0.85 to ±1.
- **Haas-free width.** `delay` at 8–25 ms on one side sounds wide and collapses
  to comb filtering in mono. Prefer `chorus`, or two genuinely different voices
  panned apart (`permafrost`'s two freeze clouds at ±0.45).
- **Width automation.** `arr_wide.cv -> field.width : 0.5` opens the breakdown
  and closes the drop. Narrow = loud.
- **Check `stereo.corr`.** Route the master through a `stereo` and `scope` its
  `corr` output. Below 0 means phase problems in mono.

## The mix bus order

```text
band  = mix { levels: [0.9, 0.7, 0.5, 0.6] }   # drums, bass, the dry elements
space = reverb { mix: 1 }                      # wet-only returns
echo  = delay  { mix: 1, time: 0.375 }
master_mix = mix { levels: [1, 1, 0.45, 0.45] }  # band L/R + returns, ducked
```

Delay/reverb on their own mixer channels use `mix: 1` (wet-only). Inserts use
`mix < 1`. Never send to a reverb from before a part's mute — tap the send from
the post-fader `vca` or you get ghosts of muted parts.

## Reading `--stats`

```bash
mise run render -- patches/x.patch -d 120s -o /tmp/x.wav --stats 4s
```

| Reading | Meaning |
|---|---|
| RMS steps at the wrong second | grid/div error, not a mix problem |
| crest (peak − RMS) < 6 dB | over-limited; back off drive |
| crest > 18 dB in a dense section | something is too transient, or too quiet |
| DC ≠ 0 | pulse at `pw != 0.5`, `math{abs}`, or a folder — add `dcblock` |
| peak > limiter threshold | something bypasses the chain |
| flat, unchanging RMS for a whole section | nothing in the arrangement moved |

## Genre loudness targets (drop-window RMS, mastered)

| Genre | RMS | Crest | Note |
|---|---|---|---|
| club techno / trance / house | −12 dB | ~11 dB | dense, hot |
| dub techno | −14 dB | ~13 dB | space is the point |
| DnB / dubstep | −13 dB | ~14 dB | transient punch over density |
| IDM | −16 dB | ~16 dB | dynamics preserved |
| ambient / drone | −20 to −26 dB | ~18 dB+ | never limit for loudness |

Ambient mastered to club loudness stops being ambient. This is not a
technicality; it is the genre.
