# Core Audio Quality Design

## Goal

Make the smallest changes that improve the sound of most ratsnest patches:
replace the ordinary VCO's weak high-frequency anti-aliasing and stop the
engine from silently hard-clipping the final float signal.

## Evidence

The current two-sample polyBLEP regression accepts a single alias component at
-20 dB relative to the fundamental, and triangle is explicitly naive. A
Wolfram model of the complete one-sided spectrum measured the current saw's
integrated alias-to-harmonic ratio from about -39 dB at 220 Hz to -17 dB at
8 kHz. A linearly interpolated, exactly band-limited wavetable reference
measured between -74 dB and -122 dB over the same sweep. This establishes that
the table lookup itself has ample margin and that source band-limiting, not
interpolation order, is the high-leverage change.

The engine currently clamps every final sample to `[-1, 1]`. This converts any
overload into an implicit, non-oversampled hard clip and also throws away the
headroom that a float WAV can represent.

## Oscillator design

Add a shared, immutable bank of band-limited saw and triangle tables. Build it
once on the non-realtime initialization path using additive harmonic series;
all VCO instances share it. Tables use 2048 samples and linear interpolation.
The bank provides sufficiently close harmonic ceilings that selecting the
brightest table whose highest partial remains below Nyquist does not audibly
dull notes near a table boundary.

Sine remains analytic. Saw reads the selected saw table. Triangle reads the
selected triangle table. Pulse is constructed from two phase-shifted
band-limited saw reads plus the existing DC term, preserving arbitrary pulse
width without requiring a separate table for every width:

`pulse(p, pw) = saw(p - pw) - saw(p) + 2*pw - 1`.

Frequency, v/oct, audio-rate FM, PWM, phase continuity, output conventions,
and state migration remain compatible. Hard sync still resets phase with the
existing timing semantics. Correcting the arbitrary hard-sync discontinuity
requires a residual queue and is deliberately outside this 80/20 pass; tests
will prevent claiming sync is alias-free.

The table bank is initialized only from `Module::init()`, never from
`process()`. Processing performs no allocation, locking, or I/O.

## Output design

`Engine::process()` will sanitize non-finite module outputs as it does today,
but copy the final stereo samples without amplitude clamping. This preserves
internal/output float headroom and makes overload explicit. Existing patches
that stay within range are bit-for-bit unaffected by this part of the change.

The live PipeWire boundary will retain safety clipping immediately before
writing to the audio device. Offline float-WAV rendering remains unclipped so
users can inspect or master overloads instead of receiving undocumented hard
clipping. The existing explicit `clipper` and `limiter` modules remain the
patch-level tools for intentional peak control.

## Validation

Tests will be written before production changes and will establish:

- integrated non-harmonic energy for coherent saw tones across low, mid, and
  high pitches is below a materially stricter threshold;
- triangle receives the same high-frequency anti-aliasing protection;
- pulse preserves its polarity, amplitude convention, and DC behavior;
- hard-sync timing and oscillator frequency remain compatible;
- `Engine::process()` preserves finite samples above unity;
- the live output boundary still contains samples to `[-1, 1]`;
- the full normal, warnings-as-errors, and sanitizer suites remain green.

Wolfram Language remains the reference oracle for spectral sweeps and table
construction analysis; production code and tests remain dependency-free C++20.
