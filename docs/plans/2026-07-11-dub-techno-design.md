# Generative Dub Techno Patch — Design

## Goal

Add a self-running dub-techno patch inspired by the restrained, hypnotic
language of Basic Channel: sparse minor stabs, deep pulse, dark saturated
echoes, and slow timbral evolution without a conventional lead melody.

## Architecture

A phase-locked clock tree drives quarter-note kicks, sparse hats, and chord
stabs roughly every two bars. A seeded random source is sampled only at chord
boundaries and quantized to a narrow D-minor/Dorian pitch set. This keeps the
harmony centered and makes every render repeatable.

Each stab combines slightly detuned oscillators into a minor-chord voicing. A
short function envelope opens a driven ladder filter, while the quiet dry
signal feeds a dark feedback delay and then a stereo plate. Slow,
unsynchronized modulators shift cutoff, delay time and tone, feedback, reverb
decay, and wet balance over tens of seconds. Kick-envelope ducking clears the
low end without conspicuous pumping.

## Musical Behavior

- Tempo is approximately 122 BPM.
- Harmony stays in a small D-minor/Dorian neighborhood, favoring the root,
  minor third, and fifth rather than producing a melodic line.
- Chord events are sparse enough that their echo tails become the principal
  rhythmic and harmonic material.
- A deep kick anchors quarter notes; filtered noise hats appear quietly and
  leave substantial negative space.
- The texture should build a smoky echo field over 20–40 seconds, periodically
  thin out, and remain stable indefinitely.
- Feedback remains below runaway and the plate does not freeze. Soft drive
  provides density without clipping the master bus.

## Scope

Create `patches/dub-techno.patch`. No synth-engine or DSL changes are needed.
The patch should use existing clock, divider, random, sample-and-hold,
quantizer, oscillator, function, VCA, ladder/SVF, drive, delay, reverb, mixer,
and output modules.

## Verification

Run the patch checker, render approximately one minute, and inspect the WAV
for finite samples, non-silence, sensible RMS, and safe peak level. Listen to
the render when playback is available. Success means a stable self-running
groove, audible long-term timbral change, restrained event density, and no
silence, runaway feedback, or clipping.
