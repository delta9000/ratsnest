# Minor Machinery Song Design

## Title

The finished patch is titled **Minor Machinery**. The name reflects its A-minor
center and clockwork modular arrangement without reading like a module-coverage
demo.

## Goal

Create a deterministic, self-running modsynth patch targeting 4:30. The song
uses every registered module type, stays centered on A minor pentatonic, and
uses the four-bar progression F major | E major | D minor | E major
(VI–V–iv–V). Its B section must be audibly derived from its A section rather
than introducing unrelated material.

## Form and timing

Run the sixteenth-note master clock at 480 BPM, equivalent to a musical tempo
of 120 BPM. Clock thirty-four musical arrangement steps once every four bars.
Each step lasts eight seconds, so the song grid is 272 seconds (4:32), with the
last steps fading into an effects tail. Append one silent guard step so a
slightly long render does not wrap immediately back to the intro.

The form is:

- blocks 0–3: harmonic/texture intro
- blocks 4–11: A1, progressively orchestrated
- blocks 12–17: B1
- blocks 18–21: breakdown and granular freeze
- blocks 22–27: A2, denser variation
- blocks 28–31: B2, peak arrangement
- blocks 32–33: outro and master fade

## Musical material

Three bar-clocked step sequencers encode chord tones for F, E, Dm, and E.
Melodic quantizers use `[0, 3, 5, 7, 10]`, the A minor pentatonic pitch set.
The A motif is an eight-step fixed phrase with selected rests. The B motif is
derived by rotating and reversing A's interval contour, moving part of it up
an octave, and changing the trigger density. A seeded Turing register adds
repeatable ornaments without displacing the recognizable fixed motif.

The arrangement contains bass, polyphonic plucks, pad/chord voices, kick,
snare/clap, hats, a folded lead, and granular/recorded texture. Section lanes
control levels, rhythmic density, filter brightness, mutation, freeze, and
effects sends. Slew modules turn arrangement steps into musical fades.

## Module coverage

Every type in `all_modules()` is instantiated and connected with a deliberate
role:

- sources and input: `vco`, `lfo`, `noise`, `random`, `sampler`, `midi`,
  `extin`
- dynamics and tone: `vca`, `adsr`, `func`, `lpg`, `svf`, `ladder`, `fold`,
  `drive`
- control and routing: `math`, `xfade`, `mix`, `logic`, `switch`, `sh`,
  `quant`, `follow`
- timing and sequencing: `clock`, `div`, `euclid`, `bern`, `turing`,
  `stepseq`, `tempo`, `alloc`
- effects, metering, and I/O: `delay`, `reverb`, `grains`, `dcblock`, `eq`,
  `compressor`, `clipper`, `limiter`, `stereo`, `scope`, `rec`, `out`

`midi` and `extin` feed quiet optional live-performance layers. `sampler`
references an optional WAV and therefore fails safely to silence when it is
absent. These branches do not affect deterministic offline composition.
`rec` captures the final master when the patch is played or rendered.

## Signal flow and mastering

Instrument voices enter an eight-channel stereo mix. The kick is analyzed by
`follow`; its envelope ducks sustained channels and drives an external
sidechain compressor. Parallel delay, reverb, and granular returns are folded
back into the mix at conservative gains. The master chain is `dcblock` → `eq`
→ `compressor` → `stereo` → `clipper` → `limiter` → `out`, with `scope` and
`rec` attached at the end.

## Verification

Validate the DSL with `modsynth check`. Render at least the full arrangement
plus tail and inspect peak, RMS, DC, and window statistics. Check registry
coverage mechanically by comparing declarations in the patch to the registry.
Run the repository's patch/documentation tests after validation.
