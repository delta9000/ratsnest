# Per-Module Nonlinear Quality Modes

## Goal

Give the ordinary `drive` and `fold` modules a high-quality antialiased path
without erasing the raw aliasing character of existing patches. Quality is an
explicit, per-module choice in the patch file. Missing configuration preserves
the current zero-latency behavior bit for bit.

## Why this is the next 80/20 improvement

The current `drive` and `fold` functions run their nonlinear transfer curves
directly at the host sample rate. A Wolfram Fourier analysis at 48 kHz found
the following proportions of generated harmonic power above Nyquist:

| input | soft drive, 10x | hard drive, 10x | wavefold, fold 4 |
| ---: | ---: | ---: | ---: |
| 2 kHz | 0.31% | 0.68% | 1.15% |
| 4 kHz | 2.37% | 3.39% | 14.57% |
| 8 kHz | 5.19% | 6.50% | 80.69% |

That energy can fold back into the audible band. Wavefolding is the most
urgent case, but both modules benefit from a shared quality contract and
measurement framework.

## Patch interface and compatibility

Both modules gain a configuration value:

```text
raw_drive = drive { mode: soft, quality: raw }
clean_drive = drive { mode: soft, quality: clean }
raw_fold = fold { quality: raw }
clean_fold = fold { quality: clean }
```

`quality` accepts `raw` or `clean` and defaults to `raw`. Unknown values also
fall back to `raw`, consistent with existing enum-config behavior.

Raw processing remains the exact current formula and has zero latency. This is
a compatibility contract enforced by sample-for-sample tests, including when
the `quality` config is absent.

Clean processing preserves the same nominal transfer curve and nominal signal
range, but may use a different numerical realization. The causal reconstruction
filter can ring beyond the curve's `+/-1` bounds on transients, so clean paths
require downstream headroom and are not brick-wall limiters. It has exactly four
host samples of peak latency. Because clean mode is explicit and latency-bearing
processing already exists in `clipper` and `limiter`, no global delay
compensation is added. The latency is documented so parallel paths remain an
intentional patch decision.

## DSP architecture

Clean mode uses a shared, allocation-free elliptic-IIR oversampling core. The
drive module runs its existing soft or hard curve at 8x. Fold uses the same 8x
filter plus first-order periodic ADAA, which suppresses its corners more
efficiently than doubling the sample rate. The twelfth-order elliptic
interpolator and matching reconstruction filter use 0.001 dB passband ripple,
80 dB stopband attenuation, and normalized pass/stop edges 0.12/0.145.

Wolfram Language is the offline design oracle. It generated the transfer
functions, verified stability and frequency response, and measured the
complete interpolate/shape/reconstruct impulse response. Both round trips
peak at exactly host sample four. Coefficients and the reproducible design
listing are checked into the repository; Wolfram is not a runtime dependency.
Production uses cascaded biquads rather than a high-order direct form so float
roundoff cannot destabilize the filter.

Selection occurs during `Module::init()` from immutable config, not from an
audio-rate input. `process()` performs no allocation, locking, I/O, coefficient
design, or variable-work loops.

## State, hot reload, and hostile input

Clean-mode antialiasing and filter histories are migrated in
`copy_state_from()` when old and new instances use compatible quality and curve
configuration. When quality or drive curve changes, the new graph begins with
cleared quality state; the existing engine hot-swap crossfade masks that
structural change. Raw mode retains no new state.

Non-finite audio or parameter inputs must not poison persistent ADAA or filter
state. Each clean path sanitizes before updating history and must recover on
the next valid sample. Extreme drive, fold, and symmetry inputs remain bounded
by the modules' documented contracts.

## Objective quality gates

Offline evaluation compared candidate output with a high-rate Wolfram reference
that was low-pass filtered before decimation. That 308-case design sweep covered:

- 48 and 96 kHz host rates;
- coherent tones from the low register through 10 kHz;
- soft and hard drive over the full amount range;
- fold amount and symmetry over their practical range;
- rapid parameter changes and audio-rate input modulation;
- silence, DC, impulses, denormals, NaN, and infinity.

Acceptance criteria:

- absent `quality` and explicit `quality: raw` are bit-identical to the old
  implementation;
- clean output has an impulse-response peak at exactly four host samples;
- clean output matches the nominal static transfer curve after latency;
- checked-in C++ regressions reduce isolated difficult alias bins by at least
  30 dB while retaining the wanted fundamental within a 0.5x--2x guardrail;
- the offline 308-case candidate sweep reports both relative improvement and
  absolute residual rather than demanding a large ratio when raw is already
  quiet;
- no unexpected DC, instability, or non-finite output;
- every module instance remains comfortably realtime in representative patch
  benchmarks.

Wolfram reference generation should also report absolute error, wanted-harmonic
energy, alias energy, and passband gain. This prevents a falsely good result
obtained merely by dulling or attenuating the signal.

## Documentation and attribution

`docs/modules.md` documents the two quality values, their latency, and the raw
compatibility guarantee. ADAA and any regenerated oversampling design receive
implementation comments and a complete `docs/attribution.md` entry based on
primary papers or technical notes. Coefficients of unresolved provenance are
replaced rather than silently reused if they become part of the clean path.

## Explicit non-goals

- No engine-global quality switch.
- No automatic graph latency compensation.
- No change to the existing `overdrive` or `clipper` interfaces in this pass.
- No silent upgrade of existing patches.
- No Wolfram runtime dependency or generated opaque DSP code.
