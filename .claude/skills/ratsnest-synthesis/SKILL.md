---
name: ratsnest-synthesis
description: Use when designing a specific sound in ratsnest — a bass, lead, pad, drone, bell, pluck, drum, hi-hat, texture, noise bed, or effect chain — or when a voice sounds thin, dull, harsh, static, or nothing like the reference.
---

# Synthesis techniques

Voices, not tracks. Each reference is a family of recipes with the ratsnest
modules that implement it, tuned starting values, and the knob that matters.

Load `ratsnest-patching` first — `docs/modules.md` is the port authority and
every recipe here assumes you have it open.

| Reference | Covers |
|---|---|
| `references/subtractive.md` | the classic voice, filter FM, resonance, PWM, supersaw, glide |
| `references/west-coast.md` | wavefolding, LPG strike, ring mod, complex-oscillator patching |
| `references/fm-voices.md` | operator recipes: EP, bell, brass, glass, wood, growl, kick |
| `references/physical.md` | `modal` and `string`: struck, bowed, blown, sympathetic, prepared |
| `references/granular.md` | `grains`: clouds, freeze, timestretch, smear, relay, shimmer |
| `references/percussion.md` | drums from scratch, pinged filters, 808/909 anatomy, the kit subs |
| `references/noise-texture.md` | wind, rain, hiss, crackle, static beds, field-recording fakes |
| `references/bass.md` | sub, reese, acid, neuro, 808 glide, dub bass |
| `references/space.md` | delay, reverb, chorus, phaser, shimmer, dub chains, feedback nets |
| `references/lib-voices.md` | the `patches/lib/*.sub` voices: interfaces, the kit contract, **measured output levels** |
| `references/writing-subs.md` | packaging a voice as a `.sub`: `param`/`input`/`output`, the demo section, interface design |

## The one idea underneath all of them

**Excitation → resonance → shaping.** Everything on this list is those three
stages with different parts in each slot:

| | Excitation | Resonance | Shaping |
|---|---|---|---|
| subtractive | `vco` | `svf` / `ladder` | `adsr` → `vca` |
| west coast | `vco` → `fold` | (none — the folder is the timbre) | `lpg` strike |
| physical | noise burst / `strike` | `modal` / `string` | built into damping |
| percussion | click or noise | pinged filter or `modal` | `func` decay |
| granular | recorded buffer | grain window | `density`/`size`/`shape` |

If a voice sounds wrong, ask which of the three stages is at fault before
changing anything. Thin = excitation. Dull or static = resonance. Lifeless =
shaping.

## Staging, always

Every voice ends in a `vca` you can automate, *before* its sends:

```text
voice_out = vca { gain: 0 }
tone -> voice_out.in
env  -> voice_out.cv
voice_out -> mixer.in3
voice_out -> reverb.in_l : 0.3     # send tapped post-fader
```

Tapping sends before that `vca` is why muted parts leave reverb ghosts.

## Modulate the timbre, not just the level

The parameters that make a voice feel alive are almost never the amplitude:

- `fm.lvl1..lvl6` are **inputs** — operator levels are modulatable, so timbre is
  an automation target.
- `modal.structure` / `string.structure` move a voice from drum to bell to bar.
- `fold.fold` under a slow LFO brightens without touching a filter.
- `grains.position` and `size` reshape a texture from the inside.
- `delay.tone` and `reverb.decay` change what the space *is*.

A patch where only levels move sounds like a mixing desk. A patch where timbres
move sounds like an instrument.

## Common mistakes

| Symptom | Cause |
|---|---|
| Voice is thin | one oscillator, no detune, no sub, filter too high |
| Voice is harsh | resonance high with no damping; `drive.amount` past 0.3 |
| Voice is static | filter/timbre parameters are configs, not modulated inputs |
| Bass disappears on small speakers | no upper harmonic; add `drive` or a saw layer |
| Struck voice never stops ringing | `damping` ≥ 0.95 on `string` is *infinite* by design |
| Everything aliases | rich `fm` waves and upward `grains.pitch` alias on purpose |
