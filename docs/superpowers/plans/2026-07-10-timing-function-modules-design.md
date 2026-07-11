# Timing and Function Modules Design

Implement `func`, `clock`, and `div` as small audio-rate DSP modules.

## `clock`

Inputs are `bpm` (default 120), `run` (default 1), and `reset` (default 0).
Outputs are `out` and `run`. `out` is a 50% duty unipolar gate at the requested
BPM; `run` mirrors the normalized run state. A rising reset edge returns the
clock to the start of its high half-cycle. Stopping holds the phase and forces
`out` low. BPM is clamped to nonnegative values.

## `div`

Inputs are `in`, `by` (default 2), and `reset`. Output `out` is a unipolar gate.
`by` is rounded and clamped to an integer of at least one. A modulo counter
advances on rising input edges, producing a `/by` gate whose complete output
period spans `by` input periods. Even divisors have exact 50% duty; odd divisors
split high and low edges as evenly as possible. Reset clears the counter and
output. This first version intentionally does not multiply clocks.

## `func`

Inputs are `trig`, `in`, `rise`, and `fall`; rise and fall are seconds and
default to 10 ms and 100 ms. Configs are `curve` (`lin`, `exp`, `log`) and
`cycle` (`off`, `on`). A trigger rising edge starts a 0-to-1 rise followed by a
1-to-0 fall. `eor` and `eoc` are one-sample unipolar pulses. Cycle mode starts a
new rise after the fall. When no triggered envelope is active, `out` slews
toward `in`, using rise for upward motion and fall for downward motion. Zero
times complete in one sample. Curves shape normalized segment progress while
preserving exact timing and endpoints.

All modules migrate phase, counters, stages, levels, and edge memory during hot
reload. Processing allocates no memory and performs no locks or I/O.

## Patch Integration

Run one 124 BPM eighth-note master `clock`. Use `div` modules for quarter notes,
bars, chord changes, melody clocks, and arrangement timing. Replace saw-shaped
kick, tick, and arpeggio gain contours with triggered `func` envelopes. Retain
smooth sine sidechain pumping where it gives a more suitable musical curve.
