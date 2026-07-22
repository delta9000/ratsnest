# Nonlinear antialiasing measurements

Date: 2026-07-21

## Decision

**Task 1 found no acceptable concrete topology under both required gates:** at
least 30 dB clean/raw improvement and exactly one host sample of peak latency.
Implementation work that depends on a `CleanShaper` is blocked until the design
and plan add a concrete reconstruction filter and validate it with this sweep.

The evidence establishes only an **ideal sampling-factor lower bound**.  Ideal
8x raw-curve sampling cleared the seven deliberately difficult checks, whereas
1x ADAA and ideal 4x did not.  It does not establish that an implementable 8x
topology clears the full matrix or the latency gate.  In particular, extending
the current two-coefficient allpass cascade to 8x failed the quality criterion
and its round-trip impulse peaks at zero-based sample 5, not sample 1.
First-order ADAA, ideal 4x,
4x+ADAA, and the current coefficient cascade are rejected as concrete answers.

The next design iteration should start at 8x raw-curve sampling and search for
a fixed-work reconstruction filter with exactly one host-sample peak latency;
that combined design is unresolved, not selected here.  Raw mode must continue
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
frequency.  Direct linear round-trip impulses peaked at sample index 4 for 4x
and sample index 5 for the extended 8x tree (zero-based), so neither realizes
the one-host-sample peak-latency gate.  The reference had zero circular phase
and therefore no group delay to compensate.

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

## Wolfram ideal-factor results

Wolfram evaluated the representative cases stated below, not the full 308-case
matrix.  The full matrix belongs solely to the local coefficient-specific
cross-check in the next section.

First-order 1x ADAA did not reliably improve the nominal-curve match.  Selected
improvements were -0.72 dB for soft at 2 kHz, 1.65 dB for soft at 8 kHz, 1.81
dB for hard at 2 kHz, 1.54 dB for hard at 8 kHz, and -0.02/9.55/34.20 dB for
fold 4 at 2/4/8 kHz.  Best alignment was approximately 0.5 host sample and gain
projection was about 0.925 through 1.015.

The cases that demonstrated ideal 4x failure and justified measuring ideal 8x
were:

| Case (48 kHz unless noted) | 4x raw | 8x raw | 16x raw |
|---|---:|---:|---:|
| soft, 8 kHz | 31.59 dB | 64.42 dB | 129.65 dB |
| hard, 2 kHz | 29.96 dB | 39.67 dB | 56.51 dB |
| hard, 8 kHz | 27.06 dB | 51.86 dB | 54.30 dB |
| fold 4, 2 kHz | 29.26 dB | 43.65 dB | 54.46 dB |
| fold 4, 4 kHz | 28.22 dB | 40.34 dB | 56.57 dB |
| fold 4, 8 kHz | 25.59 dB | **33.77 dB** | 49.01 dB |
| fold 4, sym 0.5, 4 kHz at 96 kHz | 28.14 dB | 43.72 dB | 52.54 dB |

The bold 33.77 dB result was the lowest of these **seven representative
difficult checks**, not a full-sweep worst case.  It cleared the exact threshold
by 3.77 dB.  Their ideal-8x gain projections spanned
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
    # Nyquist is a boundary convention; reject it rather than double-count it.
    H[-1]=0
    return np.fft.irfft(H,n=N)

def frac_advance(x,d):
    X=np.fft.rfft(x); k=np.arange(len(X))
    X*=np.exp(2j*np.pi*k*d/N)
    X[-1]=X[-1].real
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

def metric(y,ref,rawerr,delay,tone,rate):
    # Resolve the fundamental phase to the equivalent delay nearest the
    # topology's low-frequency group delay, then fractionally align.
    k=tone*N//rate
    q=np.fft.rfft(y)[k]/np.fft.rfft(ref)[k]
    base=-np.angle(q)/(2*np.pi*tone/rate)
    period=rate/tone
    delay=base+round((delay-base)/period)*period
    ya=frac_advance(y,delay)
    e=ya-ref; e-=e.mean(); ac=ref-ref.mean()
    E=float(np.mean(e*e)); Er=float(np.mean(rawerr*rawerr))
    acE=float(np.mean(ac*ac))
    alpha=float(np.dot(ya-ya.mean(),ac)/np.dot(ac,ac))
    improve=10*math.log10(max(Er,1e-300)/max(E,1e-300))
    return dict(error_dbfs=10*math.log10(max(E,1e-300)), improvement_db=improve,
                gain_projection=alpha, projection_error_db=20*math.log10(max(abs(alpha),1e-300)),
                peak=float(np.max(np.abs(ya))), ref_ac_dbfs=10*math.log10(max(acE,1e-300)),delay_samples=float(delay))

def run_case(kind,rate,tone,scale,bias):
    # Two periods establish IIR and ADAA state; retain the final coherent record.
    nt=2*N; n=np.arange(nt); x=np.sin(2*np.pi*tone*n/rate)
    mh=np.arange(N*64); xh=np.sin(2*np.pi*tone*mh/(rate*64))
    ref=ideal_ref(kind,xh*scale+bias)
    xr=x[N:]; rawy=raw(kind,xr*scale+bias)
    rawerr=rawy-ref; rawerr-=rawerr.mean()
    # Fixed low-frequency group-delay alignment measured from the exact linear
    # round trip. ADAA contributes half a sample at its processing rate.
    specs=(('raw',0,False,0.),('adaa',0,True,.5),('4x',2,False,3.174168),
           ('4x_adaa',2,True,3.174168+.5/4),('8x',3,False,4.761252))
    out={}
    for name,lev,aa,d in specs:
        y=candidate(kind,x,scale,bias,lev,aa)[N:]
        out[name]=metric(y,ref,rawerr,d,tone,rate)
    # Wanted in-band nonlinear harmonics = reference AC minus coherent fundamental.
    R=np.fft.rfft(ref-ref.mean()); k=tone*N//rate
    wanted=(np.sum(np.abs(R[1:])**2)-abs(R[k])**2)*2/N**2
    out['meta']=dict(kind=kind,rate=rate,tone=tone,scale=scale,bias=bias,
                     ref_peak=float(np.max(np.abs(ref))),wanted_harmonics_dbfs=10*math.log10(max(float(wanted),1e-300)))
    return out

def summary(rows,name):
    practical=[r[name] for r in rows
               if r['raw']['error_dbfs']>-140 and r[name]['ref_ac_dbfs']>-120]
    return dict(
                practical_count=len(practical),
                worst_improvement_db=min(v['improvement_db'] for v in practical),
                median_improvement_db=statistics.median(v['improvement_db'] for v in practical),
                max_error_dbfs=max(v['error_dbfs'] for v in practical),
                max_abs_projection_error_db=max(abs(v['projection_error_db']) for v in practical),
                peak_max=max(v['peak'] for v in practical))

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
    practical=[r for r in rows if r['raw']['error_dbfs']>-140 and r['raw']['ref_ac_dbfs']>-120]
    wanted=[r['meta']['wanted_harmonics_dbfs'] for r in practical]
    nonzero=[v for v in wanted if v>-1000]
    data={'config':dict(N=N,coefficients=[A0,A1],alignment={'raw':0,'adaa':.5,'4x':3.174168,'4x_adaa':3.299168,'8x':4.761252},
                        impulse_peaks={'4x':impulse_peak(2),'8x':impulse_peak(3)}),
          'count':len(rows),'summary':{n:summary(rows,n) for n in ('raw','adaa','4x','4x_adaa','8x')},'rows':rows}
    data['wanted_summary']={'numerical_zero_count':len(wanted)-len(nonzero),
                            'nonzero_range_dbfs':[min(nonzero),max(nonzero)]}
    with open('/tmp/nonlinear_results.json','w') as f: json.dump(data,f)
    print(json.dumps({'count':len(rows),'summary':data['summary']},indent=2))
if __name__=='__main__': main()
```

## Reproduction boundary and follow-up

Symbolic identities, arbitrary-precision validation, the eight representative
ADAA/ideal-4x cases, and the seven difficult ideal 4x/8x/16x cases above ran in
the authorized offline Wolfram evaluator session `leNwYLgT`.  The 308-case
actual-coefficient table ran locally in a
temporary, uncommitted Python harness.  No C++ or tests were changed.

Before `CleanShaper` implementation can begin, a concrete 8x filter must be
added to the design/plan and tested with the same sweep.  It must simultaneously
demonstrate: minimum 30.00 dB improvement on the 261-case practical set (or a
documented stricter set), finite peaks, a preregistered gain bound, and exactly
one host-sample peak latency.  Until then, the concrete topology decision is
open and dependent implementation tasks are blocked.
