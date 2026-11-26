---
name: ratsnest-surge
description: Use when a ratsnest patch loads Surge XT (or any CLAP synth) through the `plugin` module — choosing a Surge oscillator or Twist engine, writing its `set:` list, driving its params from the tracker, or when a Surge part is silent, flat, mono, too loud, or ignores velocity.
---

# Surge XT inside ratsnest

Surge runs headless as a `plugin`: no GUI, no preset file, no saved state.
The sound is the `set:` list written in the patch; the movement is ratsnest
cables into `p1`..`p8`. Load `ratsnest-patching` first; `docs/plugin.md` is
the port authority and has the host's timing rules.

Every number here was measured on Surge XT 1.3.4. Worked patches:
`patches/headless/` (one oscillator per track) and `patches/riffs/`
(tracker-driven, with a native A/B).

## Setup

Surge must be installed for its parts to sound; without it the patch
checks and plays with those parts silent.

```sh
curl -L -o surge-xt.deb https://github.com/surge-synthesizer/releases-xt/releases/download/1.3.4/surge-xt-linux-x64-1.3.4.deb
apt-get install -y ./surge-xt.deb      # the CLAP lands in /usr/lib/clap
./build/ratsnest plugins               # lists "Surge XT" and "Surge XT Effects"
```

## The loop

1. **Find names.** `ratsnest plugins "Surge XT" "A Osc 1 Type=Twist" "A Osc 1 Engine=Chords"`
   prints every param *after* those sets, with its name, normalized value,
   Surge's own text in brackets, and the value as a `p` base. Grep it.
   Names change when a type changes ("A Osc 1 Morph" exists only once the
   osc is Wavetable), so always list with the sets you will use.
2. **Write the `set:` list in order:** type first, then that type's params.
   Values are Surge's display text: `"A Filter 1 Cutoff=3000 Hz"`,
   `"A Amp EG Decay=350 ms"`, `"A Play Mode=Mono (Fingered Portamento)"`.
3. **Check what each set became.** Read the bracketed text in the listing.
   An unknown *name* is a load error (warning, and the plugin goes silent).
   An unknown *enum value* is worse: Surge silently takes the first entry.
4. **Bind `params:` for what moves.** `p1`..`p8` are normalized 0..1 and
   bound after the sets. Cable bases and modulation onto them as usual.
5. **Render with taps and measure** (`--tap name=strip`). Surge parts vary
   wildly in level by engine; balance by strip `gain`, not by guessing.

## Gotchas that cost a render

- **Velocity is flat above about 0.2.** At the default `A Velocity > VCA Gain`
  (0 dB) velocity does nothing; at -48 dB a note rises about 8 dB from vel
  0.05 to 0.2 and no further. For accents:
  `"A Velocity > VCA Gain=-48 dB"` in `set:` and `song.vel2 -> synth.vel : 0.2`.
  Velocity 0 releases the note. Twist engines react to velocity on their
  own (a Particle Noise held at vel 0.8 came out 10 dB under vel 0.2).
- **Twist params are renamed per engine, so bind them by id.** Ids stay put:
  `params: ["627352114"]` is Osc 1's engine whatever it is, 627352115 to
  627352117 its first knob, second knob and decay. Read ids from the
  listing; other osc slots have their own.
- **Scene params are global, not per voice.** Changing Osc 2 pitch or the
  Twist engine also changes notes still ringing in their release. Use it
  (tails bend when the next row switches engine) or shorten the release.
- **Twist is close to mono.** An album track built on it measured side/mid
  0.04. Twist has no unison: add width with Surge's Ensemble or Chorus FX,
  a ratsnest `chorus`, or pan a second instance.
- **Engine levels differ by 15 to 20 dB.** Modal Resonator and 2-op FM come
  out far hotter than Analog Kick/Snare/Hi-Hat, which is more than velocity
  can make up. Particle Noise and the Chords engine behind a ladder are very
  quiet (strip gains of 14 to 16 were needed). Keep hot engines on their
  own instance.
- **String Stiffness above about 0.75 (normalized) goes near-silent.**
- **Use the synth's own FX slots** (`FX A1`..`FX A4`, `"FX A1 FX Type=Spring Reverb"`).
  The separate "Surge XT Effects" plugin ignores an FX Type set. FX param
  names change with the type: list after setting it.
- **Renders are not bit-identical** (random phase and drift). Loudness
  repeats to about 0.1 LU, so measure, but don't diff WAVs.
- **CPU and RT safety.** Surge allocates inside its process(), so
  `test_rt_alloc` skips plugin patches when CLAP plugins are installed.
  Four instances made a render about a third slower than the native A/B.

## Playing it from the tracker

- **One note at a time** from `gate`/`voct`/`vel`, 64 samples (1.3 ms) late
  like everything through `plugin`.
- **Slides:** `"A Play Mode=Mono (Fingered Portamento)"`, `"A Portamento=45 ms"`,
  and `~` slurs in the `.trk`; the host sends the new note before releasing
  the old one.
- **P-locks:** put the param in a tracker column (`cva`/`cvb`/`cvd`) and
  cable it to a `p`. A param's first change in a block lands on its sample,
  ahead of a note on the same sample, so a row that sets cutoff and plays a
  note gives that note the new cutoff. `locks.patch` locks bass cutoff and
  resonance per step this way.
- **Chords from one instance:** three oscillators, Osc 2 and Osc 3 pitch as
  tracker columns, so the chord quality is locked per chord (minor .714,
  major .786, sus4 .857, fifth below .143). Or Twist's Chords engine with
  its type and inversion locked. Or one instance per voice.
- **One instance as a whole kit:** the Twist engine is a column, so each row
  is kick, snare or hat (`engine-room.patch`).
- **Movement:** Surge's own LFOs and mod matrix aren't reachable headless.
  Cable ratsnest `lfo`s at incommensurate rates onto Morph, cutoff,
  density and so on; that is where the life comes from.

Formulas and value tables (cutoff in Hz, osc pitch, the Twist engine
values, FX type names): `references/values.md`.

## When native is the better choice

Measured A/B (`locks.patch` against `locks-native.patch`, same `.trk`):
with the same cutoff locks the native ladder bass moved *more* than
Surge's, and the mixes matched in band balance. Choose Surge for what
ratsnest lacks: wavetables and Window, per-oscillator unison, Twist's drum,
chord, speech and particle engines, and its effects (spring, tape,
ensemble, Nimbus). Choose native for filter-and-envelope voices,
deterministic renders, RT-safe live play, and anything a `.sub` in
`patches/lib/` already does.
