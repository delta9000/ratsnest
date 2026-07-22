# Nonlinear antialiasing measurements

Date: 2026-07-21

## Decision

Use one uniform **8x raw-curve oversampling** topology for the future clean
mode of `drive` and `fold`.  Do not use first-order ADAA for the nominal clean
path: it changes the transfer response too much in this comparison, and adding
it at 4x was not consistently better.  Four-times raw oversampling is cheaper,
but demonstrated cases missed the 30 dB improvement target; this is the
required demonstrated failure that permits going beyond 4x.

This selects the sample-rate topology, not the legacy half-band implementation.
The current two-coefficient allpass cascade failed the quality criterion when
extended to 8x and has more than one host sample of phase delay.  Clean-mode
implementation therefore still needs a separately verified, fixed-work 8x
filter whose peak latency is exactly one host sample.  Raw mode must continue
to call the curves below directly and remain bit-for-bit legacy behavior.

## Curves and antiderivatives

The measurements used these exact legacy curves (Wolfram Language notation):

```wl
rawSoft[x_, amount_] := Tanh[x (1 + 9 Clip[amount, {0, 1}])]
rawHard[x_, amount_] := Clip[x (1 + 9 Clip[amount, {0, 1}]), {-1, 1}]
rawFold[x_, fold_, sym_] :=
  1 - 4 Abs[Mod[(x Max[fold, 0] + sym + 1)/4, 1] - 1/2]
```

For fold ADAA, let `u = x Max[fold, 0] + sym` and use

```wl
foldAnti[x_] := With[{k = Floor[(x + 1)/2]},
  With[{s = (-1)^k},
    s (x - 2 k)^2/2 + If[s < 0, 1, 0]]]
```

For soft and hard, the corresponding antiderivatives of `Tanh[u]` and
`Clip[u, {-1, 1}]` were `Log[Cosh[u]]` and
`If[Abs[u] <= 1, u^2/2, Abs[u] - 1/2]`.  First-order ADAA was the divided
difference `(F[u[n]] - F[u[n-1]])/(u[n] - u[n-1])`, with the raw curve at the
midpoint for the coincident-input limit.

Wolfram session `leNwYLgT` verified the fold identity

```wl
rawFold[x, 1, 0] == (2/Pi) ArcSin[Sin[Pi x/2]]
```

away from the odd-integer piecewise boundary convention.  Across 20,001
40-digit samples Wolfram reported exact zero to about 38.4 decimal digits.  On a dense
arbitrary-precision grid with odd corners omitted, `foldAnti'[x]` and
`rawFold[x, 1, 0]` had maximum difference zero.  One-sided continuity checks at
every odd corner from -17 through 17 had maximum error approximately `1e-20`;
`foldAnti` evaluates to `1/2` at each corner.  These checks meet the required
`1e-10` bound by ten or more decimal orders.

The existing `waveshape.h` is not an implementation of these nominal curves:
its soft curve is `x/Sqrt[1+x^2]`, not `Tanh[x]`, and it has no fold curve.

## Measurement protocol

The host rates were 48 and 96 kHz.  Every valid Cartesian-product case was
measured:

- coherent input tones: 250, 500, 1,000, 2,000, 4,000, 8,000, and 10,000 Hz;
- drive amounts: 0, 0.25, 0.5, 0.75, and 1, for both soft and hard;
- fold factors: 1, 2, 4, and 8;
- fold symmetry offsets: -0.5, 0, and 0.5.

That is 308 cases: `2 rates * 7 tones * (2 drive curves * 5 amounts + 4 folds
* 3 symmetries)`.  The coherent record was 12,288 host samples, which contains
an integer number of cycles at every rate/tone pair.  Stateful candidates were
run for one preceding record and measured on the second.

The planned long-linear-phase-FIR reference was refined before measurement to
an exact periodic zero-phase FFT brick-wall.  All requested tones are coherent
on a 1/250-second period, so this avoids transition-band, window, leakage, and
FIR-delay ambiguity rather than adding a nominally “long” but arbitrary FIR.
This is an explicit protocol deviation, not a claim that a long FIR was run.

For Wolfram at host rate `fs`, `n = Round[fs/250]`, `nh = 64 n`, and
`t = Range[0, nh - 1]/(64 fs)`.  After evaluating the curve, its `nh`-point
`Fourier` bins from `kmax + 2` through `nh - kmax`, where
`kmax = Floor[(fs/2)/250]`, were zeroed.  `InverseFourier` was made real and
decimation selected samples `[[1;;;;64]]`.  Thus DC and baseband harmonics
through the DFT-bin Nyquist convention were retained, there was no window or
FIR length, circular phase was zero, and decimation began at the first sample.
Ideal 4x, 8x, and 16x candidates used the same operation at their rates.

The local coefficient-specific cross-check used the same periodic brick-wall
idea on its longer record: a 64x real FFT, retention of bins through the host
Nyquist (DC through bin 6,144, with the boundary Nyquist bin rejected), division
by 64, and a 12,288-point inverse real FFT.  The differing boundary convention
is immaterial to the reported cases but is stated for reproducibility.

In Wolfram, stateful ADAA was fractionally aligned by Fourier phase-shift search
over -2 through 2 host samples in 1/400-sample steps (1/200 for the earlier 1x
table).  The local candidate phase was unwrapped to the delay nearest the topology's
low-frequency group delay, then removed with an exact fractional Fourier
shift.  This aligns the coherent fundamental but deliberately leaves harmonic
phase dispersion in the error.  The measured ADAA delay was 0.5 host sample.
For the exact legacy allpass round trip, the low-frequency delays were about
3.174 samples at 4x and 4.761 samples at 8x; effective phase delay varied with
frequency.  The reference had zero circular phase and therefore no group delay
to compensate.

With `r` the reference, `y` the aligned candidate, and a bar denoting the
record mean, the metrics were:

```text
E(y)        = mean(((y - r) - mean(y - r))^2)
improvement = 10 log10(E(raw) / E(y))
AC wanted   = RMS-power-normalized sum of reference in-band FFT-bin energy,
              excluding DC and the input fundamental
gain alpha  = dot(y - mean(y), r - mean(r)) / dot(r - mean(r), r - mean(r))
gain error  = 20 log10(abs(alpha))
peak        = max(abs(y))
```

All candidates used the same record, bin set, DC exclusion, phase alignment,
and gain convention.  Gain was reported separately; it was not optimized away,
so attenuation cannot masquerade as antialiasing.

The acceptance threshold was at least **30.00 dB** improvement in every
practical case.  A case is practical when reference in-band AC energy exceeds
`-120 dBFS` and raw error exceeds `-140 dBFS`.  The latter is more than 80 dB
above the observed numerical/window floor and left 261 of 308 local cases.
Intentional clipping/folding is part of the reference and never excludes a
case.  Peak must remain finite, and gain projection is a separate diagnostic
rather than part of the error normalization.  No gain or peak cutoff was set
after looking at results: the preregistered numerical thresholds were `1e-10`
for the antiderivative checks and 30.00 dB with the `-120/-140 dBFS`
practical-case gates for quality.  A gain/peak limit must be declared before
the implementation-validation rerun.

## Wolfram ideal-filter results

Wolfram evaluated the representative cases stated below, not the full 308-case
matrix.  The full matrix belongs solely to the local coefficient-specific
cross-check in the next section.

First-order 1x ADAA did not reliably improve the nominal-curve match.  Selected
improvements were -0.72 dB for soft at 2 kHz, 1.65 dB for soft at 8 kHz, 1.81
dB for hard at 2 kHz, 1.54 dB for hard at 8 kHz, and -0.02/9.55/34.20 dB for
fold 4 at 2/4/8 kHz.  Best alignment was approximately 0.5 host sample and gain
projection was about 0.925 through 1.015.

The cases that demonstrated 4x failure and justified measuring 8x were:

| Case (48 kHz unless noted) | 4x raw | 8x raw | 16x raw |
|---|---:|---:|---:|
| soft, 8 kHz | 31.59 dB | 64.42 dB | 129.65 dB |
| hard, 2 kHz | 29.96 dB | 39.67 dB | 56.51 dB |
| hard, 8 kHz | 27.06 dB | 51.86 dB | 54.30 dB |
| fold 4, 2 kHz | 29.26 dB | 43.65 dB | 54.46 dB |
| fold 4, 4 kHz | 28.22 dB | 40.34 dB | 56.57 dB |
| fold 4, 8 kHz | 25.59 dB | **33.77 dB** | 49.01 dB |
| fold 4, sym 0.5, 4 kHz at 96 kHz | 28.14 dB | 43.72 dB | 52.54 dB |

The bold 33.77 dB result was the worst of these difficult 8x checks and still
cleared the exact threshold by 3.77 dB.  Their 8x gain projections spanned
0.9816 through 1.0041.  Four-times plus ADAA was generally worse at high
frequencies: soft/hard at 8 kHz measured -4.71/-4.67 dB improvement, and fold
4 at 4/8 kHz measured 26.87/20.08 dB.  This is why 4x+ADAA is not preferred
despite its attractive arithmetic cost.

## Exact legacy-coefficient cross-check

The local NumPy 2.5.1 harness copied the sample ordering and state equations in
`oversample.h` exactly, then added one identical tree level for an 8x trial.
It ran all 308 cases.  The table reports the 261-case practical set; median and
worst are improvement against raw, maximum gain error is the absolute
projection error, and peak is the largest aligned candidate sample.

| Candidate | Worst improvement | Median improvement | Max integrated error | Max gain error | Peak |
|---|---:|---:|---:|---:|---:|
| 1x raw | 0.00 dB | 0.00 dB | -4.58 dBFS | 13.09 dB | 1.000 |
| 1x ADAA | -87.77 dB | 3.36 dB | -10.97 dBFS | 19.16 dB | 1.418 |
| actual 4x raw | -74.57 dB | -3.77 dB | -4.35 dBFS | 14.73 dB | 1.356 |
| actual 4x + ADAA | -77.40 dB | -3.24 dB | -6.04 dBFS | 11.66 dB | 1.283 |
| actual cascade extended to 8x raw | -78.09 dB | -6.86 dB | -2.51 dBFS | 17.46 dB | 1.398 |

Wanted in-band harmonic energy on this practical set was numerically zero when
no nonlinear harmonic fell below Nyquist (17 cases); otherwise it ranged from
`-87.98` to `-5.20 dBFS`.  It is a property of the wanted reference, not an
error score, and was retained separately for every case.

The broad worst case is soft, amount 0, 2 kHz at 48 kHz: raw error is already
`-135.61 dBFS`, while the exact 4x and extended 8x filters introduce harmonic
phase errors at `-61.04` and `-57.52 dBFS`.  Even restricting attention to
cases with raw error above `-40 dBFS`, the extended 8x cascade's worst
improvement is -15.22 dB (hard, amount 1, 1 kHz at 48 kHz).  It therefore fails
for substantive nonlinear cases, not only numerical-floor cases.

These local numbers are not in conflict with the Wolfram ideal-filter table:
they answer a different question.  The ideal table chooses the oversampling
factor; the local table rejects reusing a phase-dispersive, roughly 40 dB
legacy half-band as the clean reconstruction filter.

## Coefficients and provenance

The existing two-path IIR half-band section is explicitly:

```text
H_a(z) = (a + z^-1) / (1 + a z^-1)
path 0: a = 0.117565365538
path 1: a = 0.541450201656
decimator output = 0.5 * (path0(w0) + path1(previous w1))
```

`Oversampler4x`/`Decimator4x` cascade this section twice.  Their source and
design method remain unresolved, as already recorded in `docs/attribution.md`.
The coefficients predate this work.  **Wolfram did not generate them**, and no
attribution or design claim should be inferred from these measurements.

## Reproduction boundary and follow-up

Symbolic identities, arbitrary-precision validation, the eight representative
ADAA/ideal-4x cases, and the seven difficult ideal 4x/8x/16x cases above ran in
the authorized offline Wolfram evaluator session `leNwYLgT`.  The 308-case
actual-coefficient table ran locally in a
temporary, uncommitted Python harness.  No C++ or tests were changed.

Before clean mode ships, its new 8x filter must be tested with the same sweep
and must simultaneously demonstrate: minimum 30.00 dB improvement on the
261-case practical set (or a documented stricter set), finite peaks, explicitly
bounded gain error, and exactly one host-sample peak latency.  If no such 8x
filter can satisfy the latency constraint, the topology decision must be
reopened rather than silently using the current coefficients.
