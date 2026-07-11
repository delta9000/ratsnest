# Mastering modules — design

## Architecture

Add six stereo-first, independently patchable modules. Every processor has
explicit `in_l`/`in_r` and `out_l`/`out_r`; dynamics link detection across
channels. Parameters remain audio-rate inputs unless they change allocation or
topology. Config-only values are lookahead duration, oversampling topology, EQ
band enablement/type, and enum modes. Processing allocates nothing and performs
no I/O. Every audible/filter/detector/delay state migrates on hot reload.

## Interfaces

### `dcblock`

- Inputs: `in_l`(0), `in_r`(0), `cutoff`(10 Hz, clamp 1..40)
- Outputs: `out_l`, `out_r`
- First-order high-pass, parameter smoothing around 20 ms, denormal flushing.

### `stereo`

- Inputs: `in_l`, `in_r`, `width`(1, clamp 0..2), `balance`(0, -1..1),
  `mono`(0 gate), `swap`(0 gate)
- Outputs: `out_l`, `out_r`, `mid`, `side`, `corr`
- Normalized M/S width; equal-power balance; mono and swap are level-sensed.
  Correlation uses a 300 ms exponential window and clamps -1..1.

### `compressor`

- Inputs: `in_l`, `in_r`, `side_l`, `side_r`, `threshold`(-18 dBFS),
  `ratio`(4), `knee`(6 dB), `attack`(.01 s), `release`(.1 s), `makeup`(0 dB),
  `mix`(1)
- Outputs: `out_l`, `out_r`, `gain`(linear), `gr`(positive dB reduction)
- Config: `sidechain` = `internal` (default) or `external`
- Stereo-linked max-peak feed-forward detector; Giannoulis log-domain
  soft-knee curve; separate attack/release gain-reduction smoothing;
  equal-power dry/wet. No auto makeup in v1.

### `eq`

- Inputs: `in_l`, `in_r`, `hp_freq`(20), `low_freq`(120), `low_gain`(0 dB),
  `mid1_freq`(500), `mid1_gain`(0), `mid1_q`(.707), `mid2_freq`(2500),
  `mid2_gain`(0), `mid2_q`(.707), `high_freq`(10000), `high_gain`(0)
- Outputs: `out_l`, `out_r`
- RBJ sections: second-order high-pass, low shelf, two peaking EQs, high shelf.
  Parameter smoothing 20 ms; gain clamp +/-24 dB, Q .1..18, frequency
  5 Hz..0.45*rate. Zero-gain shelves/peaks are exact bypass coefficients.

### `clipper`

- Inputs: `in_l`, `in_r`, `threshold`(-1 dBFS), `drive`(0 dB), `mix`(1)
- Outputs: `out_l`, `out_r`
- Config: `mode` = `hard` or `soft`; `oversample` fixed to `4` in v1 but kept
  as a validated config for future 1/2/8 variants.
- 4x two-stage half-band oversampling. Hard clamps at linear threshold; soft
  uses normalized tanh. Equal-power mix; shared fixed latency.

### `limiter`

- Inputs: `in_l`, `in_r`, `threshold`(0 dB drive into limiter),
  `ceiling`(-1 dBFS), `release`(.1 s)
- Outputs: `out_l`, `out_r`, `gain`, `gr`
- Config: `lookahead`(.005 s, clamp 0..0.05)
- Fixed allocated stereo delay, linked peak detector, instantaneous attack in
  detector time, smoothed release, and ceiling guarantee for finite sample
  peaks. Latency equals lookahead. It is explicitly sample-peak, not true-peak.

## Shared behavior and safety

- dB inputs convert with `10^(dB/20)`; all ranges sanitize NaN/inf before casts
  or transcendental functions.
- Stereo modules never process channels independently where that could move
  the image. Constant-power laws use normalized factors.
- Silence remains exact or flushes below `1e-20`; hostile input tests require
  finite output at 8 kHz, 44.1 kHz, 48 kHz, and 96 kHz.
- Hot reload copies all state without allocation. Config changes build new
  state; migration copies the bounded common portion of buffers.
- `docs/modules.md` is updated with exact defaults, ranges, latency, examples,
  and true-peak limitations in the same commits as implementation.

## Rejected/deferred

- No monolithic `master` module.
- No multiband compressor, dynamic EQ, automatic makeup, auto release, or
  bass-mono crossover in v1.
- No true-peak or LUFS claims until standards-conformance tests exist.
- Metering/analyze is a separate follow-up, not silently folded into DSP.
