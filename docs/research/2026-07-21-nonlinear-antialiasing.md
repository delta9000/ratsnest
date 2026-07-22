# Nonlinear antialiasing measurements

Date: 2026-07-21

## Decision

**The original one-sample gate had no acceptable concrete topology.** After the
latency budget was explicitly revised to four host samples, Wolfram designed a
causal twelfth-order elliptic round trip that peaks at sample four. The 8x
design uses normalized pass/stop edges 0.12/0.145, 0.001 dB passband ripple,
and 80 dB stopband attenuation. A frequency-scaled 16x form is used for fold.

The evidence establishes only an **ideal sampling-factor lower bound**.  Ideal
8x raw-curve sampling cleared the seven deliberately difficult checks, whereas
1x ADAA and ideal 4x did not.  It does not establish that an implementable 8x
topology clears the full matrix or the latency gate.  In particular, extending
the current two-coefficient allpass cascade to 8x failed the quality criterion
and its round-trip impulse peaks at zero-based sample 5, not sample 1.
First-order ADAA, ideal 4x,
4x+ADAA, and the current coefficient cascade are rejected as concrete answers.

The selected production direction is 8x for drive and 16x for fold, implemented
as cascaded biquads rather than a high-order direct form. Raw mode continues to
call the curves below directly and remains bit-for-bit legacy behavior.

The 8x round trip improved the eight preregistered difficult cases by
31.64--62.18 dB. On the full 308-case sweep it passed the former 30 dB ratio
gate in 158 of 253 ratio-gated cases. The misses are dominated by cases where
the metric treats tiny passband response differences as alias residual; the
former universal ratio gate is therefore retained as a diagnostic, not a
production acceptance rule. Absolute residual, fundamental gain, and the
preregistered difficult cases remain mandatory. The extreme 48 kHz, 10 kHz,
fold-8 case improved from 22.76 dB at 8x to 36.33 dB at 16x while preserving
the four-sample peak.

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
* 3 symmetries)`.  Local stimulus amplitude was exactly 1 and initial phase was
zero: `x[n] = Sin[2 Pi tone n/fs]`.  The coherent record was 12,288 host
samples, which contains an integer number of cycles at every rate/tone pair.
Stateful candidates processed two consecutive records from zero state; only
the second record was measured.

The planned long-linear-phase-FIR reference was refined before measurement to
an exact periodic zero-phase FFT brick-wall.  All requested tones are coherent
on a 1/250-second period, so this avoids transition-band, window, leakage, and
FIR-delay ambiguity rather than adding a nominally “long” but arbitrary FIR.
This is an explicit protocol deviation, not a claim that a long FIR was run.

Both implementations now exclude DC and the exact host-Nyquist bin.  For
Wolfram, a `q n`-point periodic candidate zeros spectrum indices `n/2 + 1`
through `q n - n/2 + 1`, then takes first-sample decimation `[[1;;;;q]]`.
For local NumPy, the 64x `rfft` coefficients `0..N/2` are divided by 64, the
`N/2` coefficient is set to zero, and `irfft(..., n=N)` reconstructs the
reference.  Candidate/error comparisons use positive bins `1..N/2-1`.  A
fractional shift also zeros its Nyquist coefficient, so a phase operation
cannot reintroduce excluded boundary energy.

Wolfram stateful candidates were aligned by Fourier phase-shift search over -2
through 2 host samples in 1/400-sample steps.  Local waveform fidelity used
fixed measured delays: 0 for raw, 0.5 for 1x ADAA, 3.174168 for actual 4x,
3.299168 for actual 4x+ADAA, and 4.761252 for the extended 8x tree.  The primary
magnitude-spectrum score is phase invariant and needs no alignment.
For the exact legacy allpass round trip, the low-frequency delays were about
3.174 samples at 4x and 4.761 samples at 8x; effective phase delay varied with
frequency.  Direct linear round-trip impulses peaked at sample index 4 for 4x
and sample index 5 for the extended 8x tree (zero-based), so neither realizes
the one-host-sample peak-latency gate.  The reference had zero circular phase
and therefore no group delay to compensate.

With `R[k]` and `Y[k]` the unnormalized real-DFT coefficients, `K =
{1,...,N/2-1}`, `z` the delay-aligned candidate, and a bar denoting the record
mean, the metrics are:

```text
Ealias(y)   = (2/N^2) sum(k in K, (abs(Y[k]) - abs(R[k]))^2)
Efidelity   = mean(((z - r) - mean(z - r))^2)
improvement = 10 log10(Ealias(raw) / Ealias(y))
wanted      = (2/N^2) sum(k in K except input fundamental, abs(R[k])^2)
fundamental = Z[k_input] / R[k_input]       (magnitude dB and phase degrees)
projection  = dot(z-mean(z), r-mean(r)) / dot(r-mean(r), r-mean(r))
peak        = max(abs(y))                   (physical, before alignment)
```

`Ealias` is the primary alias/timbre residual: it counts wanted-bin magnitude
corruption and energy in non-wanted bins but is invariant to phase.  `Efidelity`
is explicitly a different score and retains phase dispersion.  Fundamental
complex ratio and whole-waveform projection are reported separately; neither
is called “passband gain.”  Attenuation is not fitted out of either error.

The production gate is fixed before any new filter is evaluated.  Of the 308
cases, 253 have raw `Ealias > -100 dBFS` and require at least **30.00 dB**
improvement.  The remaining 55 have raw `Ealias <= -100 dBFS` and instead
require clean `Ealias <= -100 dBFS`; demanding another ratio below the floor is
not meaningful.  Wanted non-fundamental harmonic energy is reported separately
and does not select the gate.  Intentional clipping/folding remains part of the
reference.  Antiderivative error must remain below `1e-10`; peak must be finite.
A fundamental/projection bound still must be preregistered for implementation
validation.

## Wolfram ideal-factor results

Wolfram evaluated the representative cases stated below, not the full 308-case
matrix.  The full matrix belongs solely to the local coefficient-specific
cross-check in the next section.

Each candidate cell below is `absolute alias error dBFS / improvement dB`.
Soft and hard use `amount=1` (`scale=10`).  Fold uses `fold=4,sym=0`, except
the final 96 kHz case which uses `sym=0.5`.

| Case (48 kHz unless noted) | Raw error | 1x ADAA | ideal 4x raw | ideal 4x+ADAA | ideal 8x raw | ideal 16x raw |
|---|---:|---:|---:|---:|---:|---:|
| soft, 2 kHz | -25.94 | -37.32 / 11.37 | -124.11 / 98.17 | -53.90 / 27.95 | <-200 / >200 | <-200 / >200 |
| soft, 8 kHz | -21.93 | -41.41 / 19.48 | -53.52 / 31.59 | -53.67 / 31.74 | -86.35 / 64.42 | -151.58 / 129.65 |
| hard, 2 kHz | -22.74 | -37.75 / 15.01 | -52.70 / 29.96 | -52.69 / 29.95 | -62.41 / 39.67 | -79.25 / 56.51 |
| hard, 8 kHz | -21.69 | -35.51 / 13.82 | -48.75 / 27.06 | -54.56 / 32.87 | -73.55 / 51.86 | -75.99 / 54.30 |
| fold, 2 kHz | -22.95 | -24.86 / 1.91 | -52.16 / 29.21 | -50.81 / 27.86 | -66.55 / 43.60 | -77.36 / 54.41 |
| fold, 4 kHz | -13.49 | -24.51 / 11.02 | -41.71 / 28.22 | -41.71 / 28.22 | -53.83 / 40.34 | -70.06 / 56.57 |
| fold, 8 kHz | -13.68 | -51.80 / 38.12 | -39.27 / 25.59 | -40.33 / 26.66 | -47.45 / **33.77** | -62.69 / 49.01 |
| fold, sym 0.5, 4 kHz at 96 kHz | -25.44 | -28.34 / 2.90 | -53.64 / 28.20 | -50.77 / 25.33 | -70.30 / 44.85 | -77.70 / 52.26 |

`>200` marks a numerical-floor result, not evidence for hundreds of decibels
of realizable rejection; evaluator and local equivalents diverged there while
agreeing on meaningful cases.  Ideal 4x raw misses 30 dB in five representative
cases, and ideal 4x+ADAA misses in five.  The bold 33.77 dB is the lowest of the
seven deliberately difficult ideal-8x checks, not a full-sweep worst case.
Thus ideal evidence says only that the concrete-filter search starts at 8x.

Under the revised magnitude metric, toggling only exact-Nyquist inclusion
changed no meaningful representative value by 0.01 dB; soft 2 kHz at ideal
16x moved 0.14 dB, but both values were below the numerical floor.  Differences
from the earlier table beyond that (notably the 96 kHz row) also reflect the
metric correction and explicit case parameters, not merely the boundary bin.

## Executable representative Wolfram analysis

The following is the complete representative-only listing validated after the
metric correction.  Wolfram CodeInspector reported no issues at confidence
0.75 with Formatting/Scoping excluded.  Authorized evaluator session
`IDL8qnbD` completed it; the only message was benign symbol shadowing from the
global-context `ClearAll`.  Meaningful results agree with the independently
implemented local equivalent.  It does not run the 308-case coefficient sweep.

```wl
ClearAll["Global`*"];
$MaxExtraPrecision = 1000;

rawSoft[x_, amount_] := Tanh[x (1 + 9 Clip[amount, {0, 1}])];
rawHard[x_, amount_] := Clip[x (1 + 9 Clip[amount, {0, 1}]), {-1, 1}];
rawFold[x_, fold_, sym_] :=
  1 - 4 Abs[Mod[(x Max[fold, 0] + sym + 1)/4, 1] - 1/2];
foldAnti[x_] := With[{k = Floor[(x + 1)/2]},
  With[{s = (-1)^k}, s (x - 2 k)^2/2 + If[s < 0, 1, 0]]];

rawU["soft", u_] := Tanh[u];
rawU["hard", u_] := Clip[u, {-1, 1}];
rawU["fold", u_] := 1 - 4 Abs[Mod[(u + 1)/4, 1] - 1/2];
antiU["soft", u_] := Log[Cosh[u]];
antiU["hard", u_] := If[Abs[u] <= 1, u^2/2, Abs[u] - 1/2];
antiU["fold", u_] := foldAnti[u];

adaaRun[kind_, onePeriod_List] := Module[
  {u = Join[onePeriod, onePeriod], prev = 0., out, dx, i},
  out = ConstantArray[0., 2 Length[onePeriod]];
  Do[dx = u[[i]] - prev;
    out[[i]] = If[Abs[dx] < 10^-10,
      rawU[kind, (u[[i]] + prev)/2],
      (antiU[kind, u[[i]]] - antiU[kind, prev])/dx];
    prev = u[[i]], {i, Length[u]}];
  Take[out, -Length[onePeriod]]];

(* y contains q samples per host sample for one coherent period.  The exact
   host Nyquist coefficient and every higher-frequency coefficient are zeroed.
   Default Fourier convention is used consistently; first-sample decimation. *)
periodicProject[y_List, q_Integer, n_Integer] := Module[{s = Fourier[y], nh},
  nh = Length[y];
  s[[n/2 + 1 ;; nh - n/2 + 1]] = 0;
  Re[InverseFourier[s][[1 ;; ;; q]]]];

makeSignal[case_, q_Integer, useADAA_: False] := Module[
  {fs = case["fs"], f = case["tone"], n, t, x, u, y},
  n = Round[fs/250]; t = Range[0, q n - 1]/(q fs);
  x = Sin[2 Pi f t];
  u = case["scale"] x + case["bias"];
  y = If[useADAA, adaaRun[case["kind"], u], rawU[case["kind"], u]];
  periodicProject[y, q, n]];

makeReference[case_] := makeSignal[case, 64, False];

fracAdvance[x_List, d_] := Module[{n = Length[x], s, signedK},
  s = Fourier[x]; s[[n/2 + 1]] = 0;
  signedK = Join[Range[0, n/2], Range[-n/2 + 1, -1]];
  Re[InverseFourier[s Exp[2 Pi I signedK d/n]]]];

measure[y_List, ref_List, raw_List, case_] := Module[
  {n = Length[ref], ys, rs, ws, pos, aliasE, rawE, delays, fidelity,
   best, aligned, k, fund, alpha},
  ys = Fourier[y]; rs = Fourier[ref]; ws = Fourier[raw];
  pos = Range[2, n/2]; (* DC and exact Nyquist excluded. *)
  aliasE = 2 Total[(Abs[ys[[pos]]] - Abs[rs[[pos]]])^2]/n^2;
  rawE = 2 Total[(Abs[ws[[pos]]] - Abs[rs[[pos]]])^2]/n^2;
  delays = Range[-2, 2, 1/400];
  fidelity = Table[With[{z = fracAdvance[y, d]},
    {Mean[(z - ref - Mean[z - ref])^2], d, z}], {d, delays}];
  best = First@SortBy[fidelity, First]; aligned = best[[3]];
  k = Round[case["tone"]/250] + 1;
  fund = Fourier[aligned][[k]]/rs[[k]];
  alpha = ((aligned - Mean[aligned]).(ref - Mean[ref]))/
    ((ref - Mean[ref]).(ref - Mean[ref]));
  <|"alias_error_dbfs" -> 10 Log10[Max[aliasE, 10^-300]],
    "alias_improvement_db" -> 10 Log10[Max[rawE, 10^-300]/Max[aliasE, 10^-300]],
    "fidelity_error_dbfs" -> 10 Log10[Max[best[[1]], 10^-300]],
    "delay" -> best[[2]], "fundamental_mag_db" -> 20 Log10[Abs[fund]],
    "fundamental_phase_deg" -> Arg[fund]/Degree,
    "waveform_projection" -> alpha, "physical_peak" -> Max[Abs[y]]|>];

cases = {
  <|"name" -> "soft2k", "kind" -> "soft", "fs" -> 48000,
    "tone" -> 2000, "amount" -> 1, "scale" -> 10, "bias" -> 0|>,
  <|"name" -> "soft8k", "kind" -> "soft", "fs" -> 48000,
    "tone" -> 8000, "amount" -> 1, "scale" -> 10, "bias" -> 0|>,
  <|"name" -> "hard2k", "kind" -> "hard", "fs" -> 48000,
    "tone" -> 2000, "amount" -> 1, "scale" -> 10, "bias" -> 0|>,
  <|"name" -> "hard8k", "kind" -> "hard", "fs" -> 48000,
    "tone" -> 8000, "amount" -> 1, "scale" -> 10, "bias" -> 0|>,
  <|"name" -> "fold2k", "kind" -> "fold", "fs" -> 48000,
    "tone" -> 2000, "fold" -> 4, "sym" -> 0, "scale" -> 4, "bias" -> 0|>,
  <|"name" -> "fold4k", "kind" -> "fold", "fs" -> 48000,
    "tone" -> 4000, "fold" -> 4, "sym" -> 0, "scale" -> 4, "bias" -> 0|>,
  <|"name" -> "fold8k", "kind" -> "fold", "fs" -> 48000,
    "tone" -> 8000, "fold" -> 4, "sym" -> 0, "scale" -> 4, "bias" -> 0|>,
  <|"name" -> "fold4k96sym5", "kind" -> "fold", "fs" -> 96000,
    "tone" -> 4000, "fold" -> 4, "sym" -> .5, "scale" -> 4, "bias" -> .5|>};

results = Association@Table[Module[{ref = makeReference[c], raw, candidates},
  raw = makeSignal[c, 1, False];
  candidates = <|"raw" -> raw, "adaa" -> makeSignal[c, 1, True],
    "4x" -> makeSignal[c, 4, False], "4x_adaa" -> makeSignal[c, 4, True],
    "8x" -> makeSignal[c, 8, False], "16x" -> makeSignal[c, 16, False]|>;
  c["name"] -> Association@KeyValueMap[#1 -> measure[#2, ref, raw, c] &,
    candidates]], {c, cases}];
Export["/tmp/nonlinear-ideal-results.json", results, "RawJSON"];
results
```

## Exact legacy-coefficient cross-check

The local NumPy 2.5.1 harness matches the C++ equations, state update, branch
order, and cascade structure, then adds one identical tree level for an 8x
trial.  It is not bit-exact production correspondence: Python uses float64 and
does not reproduce C++ float rounding or the denormal flush in `Allpass`.

All 308 cases were classified: 253 ratio-gated and 55 floor-gated.  “Worst” and
“median” below apply to ratio-gated magnitude improvement; maximum alias error
and gate passes cover all cases.

| Candidate | Worst improvement | Median improvement | Max alias error | Ratio/floor passes |
|---|---:|---:|---:|---:|
| 1x raw | 0.00 dB | 0.00 dB | -4.58 dBFS | 0/55 (55 total) |
| 1x ADAA | -39.16 dB | 2.79 dB | -11.33 dBFS | 3/0 (3 total) |
| actual 4x raw | -1.20 dB | 15.17 dB | -7.87 dBFS | 37/43 (80 total) |
| actual 4x+ADAA | -29.30 dB | 9.62 dB | -13.09 dBFS | 0/3 (3 total) |
| extended actual 8x raw | -3.04 dB | 14.13 dB | -7.95 dBFS | 4/43 (47 total) |

The separate fidelity and transfer diagnostics are:

| Candidate | Max fidelity error | Max abs fundamental magnitude | Max abs fundamental phase | Max abs projection (dB) | Physical peak |
|---|---:|---:|---:|---:|---:|
| 1x raw | -4.58 dBFS | 13.09 dB | 0.00 deg | 13.09 dB | 1.000 |
| 1x ADAA | -10.97 dBFS | 7.51 dB | 0.00 deg | 19.16 dB | 1.000 |
| actual 4x raw | -5.24 dBFS | 10.65 dB | 73.03 deg | 10.30 dB | 1.446 |
| actual 4x+ADAA | -6.60 dBFS | 4.77 dB | 59.52 deg | 9.73 dB | 1.382 |
| extended actual 8x raw | -3.10 dBFS | 7.20 dB | 53.83 deg | 38.54 dB | 1.404 |

Wanted non-fundamental in-band harmonic energy was numerically zero in 45
cases; otherwise it ranged from `-87.98` to `-5.20 dBFS`.  It is reference
classification, not alias error.

The worst ratio-gated case for actual 4x and extended 8x is soft at 48 kHz,
500 Hz, amount 0.75: raw alias error is `-92.04 dBFS`; actual 4x is
`-90.84 dBFS` (-1.20 dB improvement) and extended 8x is `-89.00 dBFS`
(-3.04 dB).  Absolute extended-8x alias error reaches `-7.95 dBFS` for fold 8,
10 kHz at 48 kHz.  The existing cascade therefore fails the primary magnitude
and latency gates, with poor fidelity diagnostics as shown separately above.

Exact-Nyquist exclusion changed alias error by more than 0.005 dB in 108 raw,
91 ADAA, 86 actual-4x, 102 actual-4x+ADAA, and 108 extended-8x cases.  Maximum
changes were 19.39, 59.71, 51.95, 33.80, and 48.19 dB respectively; the largest
values occur where reference or candidate places strong energy exactly at
Nyquist or the remaining error is near the numerical floor.  Those maxima were
respectively soft amount 0 at 8 kHz, soft amount 0.75 at 8 kHz, soft amount 0
at 8 kHz, fold 2/sym 0 at 8 kHz, and soft amount 0 at 8 kHz, all at 48 kHz.
This is why the boundary is now identical and excluded everywhere.

These local numbers are not in conflict with the Wolfram ideal-factor table:
they answer a different question.  The ideal table supplies evidence that the
search must start at 8x; it does not choose a topology.  The local table rejects
reusing a phase-dispersive, roughly 40 dB legacy half-band as the clean
reconstruction filter.

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

## Executable local cross-check

Save the following as `analyze_nonlinear.py` and run it with Python 3 and
NumPy 2.5.1.  It writes the per-case records and exact table aggregates to
`/tmp/nonlinear_results.json`.  The listing is the local harness used for the
reported rerun; no omitted helper or unstated state is required.

```python
#!/usr/bin/env python3
import json, math, statistics
import numpy as np

A0=0.117565365538
A1=0.541450201656
RATES=(48000,96000)
TONES=(250,500,1000,2000,4000,8000,10000)
AMOUNTS=(0,.25,.5,.75,1)
FOLDS=(1,2,4,8)
SYMS=(-.5,0,.5)
N=12288

class AP:
    def __init__(self): self.s=0.0
    def p(self,x,a):
        y=a*x+self.s; self.s=x-a*y; return y
class HB:
    def __init__(self): self.p0=AP(); self.p1=AP(); self.prev=0.0
    def up(self,x): return self.p0.p(x,A0),self.p1.p(x,A1)
    def dn(self,x0,x1):
        v0=self.p0.p(x0,A0); v1=self.p1.p(self.prev,A1); self.prev=x1
        return .5*(v0+v1)
class OS:
    def __init__(self,levels):
        self.levels=levels
        self.upbanks=[[HB() for _ in range(1<<j)] for j in range(levels)]
        self.dnbanks=[[HB() for _ in range(1<<(levels-j-1))] for j in range(levels)]
    def up(self,x):
        a=[x]
        for bank in self.upbanks:
            b=[]
            for hb,v in zip(bank,a): b.extend(hb.up(v))
            a=b
        return a
    def dn(self,a):
        for bank in self.dnbanks:
            a=[hb.dn(a[2*i],a[2*i+1]) for i,hb in enumerate(bank)]
        return a[0]

def raw(kind,u):
    if kind=='soft': return np.tanh(u)
    if kind=='hard': return np.clip(u,-1,1)
    return 1-4*np.abs(np.mod((u+1)/4,1)-.5)
def scalar_raw(kind,u):
    if kind=='soft': return math.tanh(u)
    if kind=='hard': return min(1,max(-1,u))
    return 1-4*abs(((u+1)/4%1)-.5)
def anti(kind,x):
    if kind=='soft':
        a=abs(x); return a+math.log1p(math.exp(-2*a))-math.log(2)
    if kind=='hard':
        a=abs(x); return .5*x*x if a<=1 else a-.5
    k=math.floor((x+1)/2); s=-1 if k&1 else 1
    return s*(x-2*k)**2/2+(1 if s<0 else 0)
class ADAA:
    def __init__(self,kind): self.kind=kind; self.x1=0.
    def p(self,x):
        dx=x-self.x1
        if abs(dx)<1e-10: y=scalar_raw(self.kind,.5*(x+self.x1))
        else: y=(anti(self.kind,x)-anti(self.kind,self.x1))/dx
        self.x1=x; return y

def ideal_ref(kind,u_hi):
    y=raw(kind,u_hi)
    Y=np.fft.rfft(y)
    H=Y[:N//2+1]/64
    included_nyquist_magnitude=float(abs(H[-1]))
    # Nyquist is a boundary convention; reject it rather than double-count it.
    H[-1]=0
    return np.fft.irfft(H,n=N),included_nyquist_magnitude

def frac_advance(x,d):
    X=np.fft.rfft(x); k=np.arange(len(X))
    X*=np.exp(2j*np.pi*k*d/N)
    # Exact Nyquist is outside every reference and comparison bin set.
    X[-1]=0
    return np.fft.irfft(X,n=N)

def candidate(kind,x,scale,bias,levels,adaa):
    if levels==0:
        u=x*scale+bias
        if not adaa: return raw(kind,u)
        sh=ADAA(kind); out=np.empty(len(u))
        for i,v in enumerate(u): out[i]=sh.p(float(v))
        return out
    os=OS(levels); sh=ADAA(kind) if adaa else None; out=np.empty(len(x))
    for i,v in enumerate(x):
        us=os.up(float(v)); ys=[]
        for q in us:
            z=q*scale+bias; ys.append(sh.p(z) if sh else scalar_raw(kind,z))
        out[i]=os.dn(ys)
    return out

def metric(y,ref,ref_nyquist_magnitude,delay,tone,rate):
    # Alias/timbre score is phase invariant and excludes DC and exact Nyquist.
    Y=np.fft.rfft(y); R=np.fft.rfft(ref)
    alias_E=float(2*np.sum((np.abs(Y[1:-1])-np.abs(R[1:-1]))**2)/N**2)
    alias_E_with_nyquist=alias_E+float((abs(Y[-1])-ref_nyquist_magnitude)**2/N**2)
    # Fidelity score uses the topology's fixed delay and retains phase dispersion.
    ya=frac_advance(y,delay)
    e=ya-ref; e-=e.mean(); ac=ref-ref.mean()
    fidelity_E=float(np.mean(e*e))
    acE=float(np.mean(ac*ac))
    alpha=float(np.dot(ya-ya.mean(),ac)/np.dot(ac,ac))
    k=tone*N//rate
    fundamental=np.fft.rfft(ya)[k]/R[k]
    return dict(alias_error_dbfs=10*math.log10(max(alias_E,1e-300)),
                alias_error_with_nyquist_dbfs=10*math.log10(max(alias_E_with_nyquist,1e-300)),
                nyquist_exclusion_delta_db=10*math.log10(max(alias_E,1e-300)/max(alias_E_with_nyquist,1e-300)),
                fidelity_error_dbfs=10*math.log10(max(fidelity_E,1e-300)),
                fundamental_mag_db=20*math.log10(max(abs(fundamental),1e-300)),
                fundamental_phase_deg=float(np.angle(fundamental,deg=True)),
                waveform_projection=alpha,
                waveform_projection_db=20*math.log10(max(abs(alpha),1e-300)),
                physical_peak=float(np.max(np.abs(y))),
                ref_ac_dbfs=10*math.log10(max(acE,1e-300)),delay_samples=float(delay))

def run_case(kind,rate,tone,scale,bias):
    # Two periods establish IIR and ADAA state; retain the final coherent record.
    nt=2*N; n=np.arange(nt); x=np.sin(2*np.pi*tone*n/rate)
    mh=np.arange(N*64); xh=np.sin(2*np.pi*tone*mh/(rate*64))
    ref,ref_nyquist_magnitude=ideal_ref(kind,xh*scale+bias)
    xr=x[N:]; rawy=raw(kind,xr*scale+bias)
    # Fixed low-frequency group-delay alignment measured from the exact linear
    # round trip. ADAA contributes half a sample at its processing rate.
    specs=(('raw',0,False,0.),('adaa',0,True,.5),('4x',2,False,3.174168),
           ('4x_adaa',2,True,3.174168+.5/4),('8x',3,False,4.761252))
    out={}
    for name,lev,aa,d in specs:
        y=candidate(kind,x,scale,bias,lev,aa)[N:]
        out[name]=metric(y,ref,ref_nyquist_magnitude,d,tone,rate)
    rawE=10**(out['raw']['alias_error_dbfs']/10)
    for name in ('raw','adaa','4x','4x_adaa','8x'):
        m=out[name]; cleanE=10**(m['alias_error_dbfs']/10)
        m['alias_improvement_db']=10*math.log10(max(rawE,1e-300)/max(cleanE,1e-300))
        if out['raw']['alias_error_dbfs']>-100:
            m['gate']='ratio'; m['passes_gate']=m['alias_improvement_db']>=30
        else:
            m['gate']='floor'; m['passes_gate']=m['alias_error_dbfs']<=-100
    # Wanted in-band nonlinear harmonics = reference AC minus coherent fundamental.
    R=np.fft.rfft(ref-ref.mean()); k=tone*N//rate
    wanted=(np.sum(np.abs(R[1:-1])**2)-abs(R[k])**2)*2/N**2
    out['meta']=dict(kind=kind,rate=rate,tone=tone,scale=scale,bias=bias,
                     ref_peak=float(np.max(np.abs(ref))),wanted_harmonics_dbfs=10*math.log10(max(float(wanted),1e-300)))
    return out

def summary(rows,name):
    vals=[r[name] for r in rows]
    ratio=[r[name] for r in rows if r['raw']['alias_error_dbfs']>-100]
    return dict(
                ratio_case_count=len(ratio),floor_case_count=len(vals)-len(ratio),
                ratio_worst_improvement_db=min(v['alias_improvement_db'] for v in ratio),
                ratio_median_improvement_db=statistics.median(v['alias_improvement_db'] for v in ratio),
                max_alias_error_dbfs=max(v['alias_error_dbfs'] for v in vals),
                max_fidelity_error_dbfs=max(v['fidelity_error_dbfs'] for v in vals),
                max_abs_fundamental_mag_db=max(abs(v['fundamental_mag_db']) for v in vals),
                max_abs_fundamental_phase_deg=max(abs(v['fundamental_phase_deg']) for v in vals),
                max_abs_waveform_projection_db=max(abs(v['waveform_projection_db']) for v in vals),
                physical_peak_max=max(v['physical_peak'] for v in vals),
                nyquist_changed_case_count=sum(abs(v['nyquist_exclusion_delta_db'])>.005 for v in vals),
                max_abs_nyquist_delta_db=max(abs(v['nyquist_exclusion_delta_db']) for v in vals),
                ratio_pass_count=sum(v['passes_gate'] for v in ratio),
                floor_pass_count=sum(v['passes_gate'] for v in vals if v['gate']=='floor'),
                pass_count=sum(v['passes_gate'] for v in vals))

def impulse_peak(levels):
    x=np.zeros(128); x[0]=1
    y=candidate('hard',x,1,0,levels,False)
    i=int(np.argmax(np.abs(y)))
    return {'sample_index':i,'magnitude':float(abs(y[i]))}

def main():
    rows=[]
    for rate in RATES:
      for tone in TONES:
        if tone>=rate/2: continue
        for a in AMOUNTS: rows.append(run_case('soft',rate,tone,1+9*a,0))
        for a in AMOUNTS: rows.append(run_case('hard',rate,tone,1+9*a,0))
        for f in FOLDS:
          for s in SYMS: rows.append(run_case('fold',rate,tone,f,s))
        print('done',rate,tone,flush=True)
    wanted=[r['meta']['wanted_harmonics_dbfs'] for r in rows]
    nonzero=[v for v in wanted if v>-1000]
    data={'config':dict(N=N,coefficients=[A0,A1],nyquist='excluded',
                        gate={'ratio_if_raw_dbfs_above':-100,'required_improvement_db':30,
                              'otherwise_clean_max_dbfs':-100},
                        alignment={'raw':0,'adaa':.5,'4x':3.174168,'4x_adaa':3.299168,'8x':4.761252},
                        impulse_peaks={'4x':impulse_peak(2),'8x':impulse_peak(3)}),
          'count':len(rows),'summary':{n:summary(rows,n) for n in ('raw','adaa','4x','4x_adaa','8x')},'rows':rows}
    data['wanted_summary']={'numerical_zero_count':len(wanted)-len(nonzero),
                            'nonzero_range_dbfs':[min(nonzero),max(nonzero)]}
    with open('/tmp/nonlinear_results.json','w') as f: json.dump(data,f)
    print(json.dumps({'count':len(rows),'summary':data['summary']},indent=2))
if __name__=='__main__': main()
```

## Reproduction boundary and follow-up

Symbolic identities and arbitrary-precision validation ran in authorized
Wolfram session `leNwYLgT`.  The corrected eight-case representative listing
ran in session `IDL8qnbD`.  The 308-case actual-coefficient table ran locally
from the Python listing embedded above.  No C++ or tests were changed.

The four-sample topology above removes the implementation block. Production
tests still need to verify the checked-in float biquads against the double
precision model, measure the exact four-sample peak, enforce the difficult-case
spectral floor, and benchmark the 8x and 16x paths under representative load.
