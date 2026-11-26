# Surge XT values for `p` cables (measured, Surge XT 1.3.4)

All `p1`..`p8` inputs are Surge's normalized 0..1. Confirm any value with
`ratsnest plugins "Surge XT" "Name=text"` and read the `p base` it prints.

## Filter cutoff

`x` normalized is `440 * 2^((130x - 60) / 12)` Hz, i.e. `13.75 Hz * 2^(10.83x)`.

| x | Hz |
|---|---|
| 0.30 | 130 |
| 0.40 | 277 |
| 0.485 | 523 (default) |
| 0.50 | 587 |
| 0.60 | 1245 |
| 0.68 | 3300 |

A native filter in the same units: `lane -> ladder.v_oct : 10.83` with
`cutoff: 13.75`.

## Oscillator pitch

"A Osc N Pitch" spans ±7 semitones: `0.5 + semitones / 14`.
+2 = .643, +3 = .714, +4 = .786, +5 = .857, +7 = 1, -5 = .143.
In v/oct for a native voice: `(x - 0.5) * 1.1667`.

## Twist engines ("A Osc 1 Engine", id 627352114 for Osc 1)

Value `0.005 + 0.066 * index`. Measured: Waveshaper 1 (.071), 2-Operator FM
(.137), Chords (.401), Filtered Noise (.599), Particle Noise (.665),
Inharmonic String (.731), Modal Resonator (.797), Analog Kick (.863), Analog
Snare (.929), Analog Hi-Hat (.995). The other indexes follow the same step
(Formant/PD, Harmonic, Wavetable, Vowels/Speech, Granular Cloud) but their
values were not read back individually.

The first knob per engine: Analog Kick Sharpness, Snare and Hi-Hat
Tone<>Noise, Modal Material, Chords Type, Inharmonic String Inharmonicity,
Particle Noise Freq Random. The third (627352117) is Decay Time on the
drum and resonator engines.

Chords' "Type" picks the chord along 0..1. `engine-room.trk` uses sus4 .2,
m .3, m7 .4, m9 .5, m11 .6, 6/9 .7, maj9 .8 and maj7 .9, which assumes
Plaits' chord order. That mapping is **unverified**: pitch-check it before
you rely on a specific chord.

## FX types ("FX A1 FX Type=...")

Delay, Reverb 1, Phaser, Rotary, Distortion, EQ, Freq Shift, Conditioner,
Chorus, Vocoder, Reverb 2, Flanger, Ring Mod, Airwindows, Neuron, Graphic EQ,
Resonator, CHOW, Exciter, Ensemble, Combulator, Nimbus, Tape, Treemonster,
Waveshaper, Mid-Side Tool, Spring Reverb, Bonsai.

The params of a slot are renamed to fit its type, e.g. "FX A1 Reverb - Decay
Time" for Reverb 2 and "FX A1 Reverb - Decay" for Spring Reverb. List the
slot after setting its type.

## Play modes ("A Play Mode=...")

Poly, Mono, Mono (Single Trigger), Mono (Fingered Portamento), and others.
Use Mono (Fingered Portamento) with "A Portamento=N ms" for tracker `~`
slides.
