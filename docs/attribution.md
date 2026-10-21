# Attribution and implementation provenance

ratsnest stands on the work of many researchers and open-source developers.
This document maps distinctive implementations to their sources and describes
how they were adapted. Legal notices for software-derived portions are retained
in [`THIRD_PARTY_NOTICES.md`](../THIRD_PARTY_NOTICES.md); ratsnest's project
license is in [`LICENSE`](../LICENSE).

Relationships are classified as:

- **Code-derived:** structure, constants, or implementation details were
  adapted from upstream source; its software license and notice apply.
- **Paper-derived:** implemented locally from a published technical
  description; cite the authors, but do not imply their paper licenses this
  source code.
- **Concept-inspired:** product or project semantics informed the design, while
  the implementation was written locally.
- **External dependency:** linked or fetched software that is not vendored.

## Implementation provenance

| ratsnest code | relationship | source | adaptation |
| --- | --- | --- | --- |
| `src/dsp/resonate.h`, `src/dsp/modules/modal.cpp`, `src/dsp/modules/string.cpp` | **Code-derived**, MIT | Emilie Gillet's [Mutable Instruments Rings](https://github.com/pichenettes/eurorack) and [stmlib](https://github.com/pichenettes/stmlib), particularly Rings' [`resonator.cc`](https://github.com/pichenettes/eurorack/blob/master/rings/dsp/resonator.cc), [`string.cc`](https://github.com/pichenettes/eurorack/blob/master/rings/dsp/string.cc), [`plucker.h`](https://github.com/pichenettes/eurorack/blob/master/rings/dsp/plucker.h), and [`part.cc`](https://github.com/pichenettes/eurorack/blob/master/rings/dsp/part.cc) | Substantially adapted into a shared modal/string core: modal-resonator mappings, string dispersion and damping, plucker, SVF, cosine recurrence, and delay/allpass primitives. Full MIT notices are retained in `THIRD_PARTY_NOTICES.md`. |
| `src/dsp/modules/string.cpp` | **Paper-derived in ancestry and code-derived through Rings** | Kevin Karplus and Alex Strong, “Digital Synthesis of Plucked-String and Drum Timbres,” *Computer Music Journal* 7(2), 1983, [DOI 10.2307/3680062](https://doi.org/10.2307/3680062) | Extends the Karplus–Strong feedback-delay family using the dispersion, damping, and excitation structure adapted through Rings. |
| `src/dsp/modules/reverb.cpp` | **Paper-derived** | Jon Dattorro, “Effect Design Part 1: Reverberator and Other Filters,” *JAES* 45(9), 1997 ([author-hosted paper](https://ccrma.stanford.edu/~dattorro/EffectDesignPart1.pdf)) | The plate topology, delay lengths, coefficients, and output taps are transcribed from the paper and sample-rate scaled from its 29,761 Hz reference. |
| `src/dsp/modules/compressor.cpp` | **Paper-derived** | Dimitrios Giannoulis, Michael Massberg, and Joshua D. Reiss, “Digital Dynamic Range Compressor Design—A Tutorial and Analysis,” *JAES* 60(6), 2012 ([paper](https://eecs.qmul.ac.uk/~josh/documents/2012/GiannoulisMassbergReiss-dynamicrangecompression-JAES2012.pdf)) | Locally implements the log-domain soft-knee static curve and attack/release gain-reduction ballistics. The detector target is peak-held for `min(release, 20 ms)` before the branching smoother (after Luff, next row) so bass does not ripple the gain. |
| `src/dsp/modules/limiter.cpp`, peak-hold in `src/dsp/modules/compressor.cpp` | **Technical-note-derived** | Geraint Luff (Signalsmith Audio), [“Designing a straightforward limiter”](https://signalsmith-audio.co.uk/writing/2022/limiter/), 2022 | Implements the article's structure locally: a moving peak-hold (minimum gain) over at least the lookahead, a release stage with a hold, and a box filter as long as the lookahead so the smoothed gain is guaranteed to reach each peak's requirement as the delayed peak arrives. Works in gain-reduction dB with a single box filter and a one-pole release; no Signalsmith code was copied. |
| `SlidingMax` in `src/dsp/mastering.h` | **Paper-derived** | Daniel Lemire, [“Streaming Maximum-Minimum Filter Using No More than Three Comparisons per Element”](https://arxiv.org/abs/cs/0610046), arXiv:cs/0610046, 2006 | The monotonic-deque running maximum, in a fixed-capacity power-of-two ring with timestamps so the window length may change per sample. |
| `src/dsp/biquad.h`, `src/dsp/modules/eq.cpp` | **Paper/technical-note-derived** | Robert Bristow-Johnson, [W3C Audio EQ Cookbook](https://www.w3.org/TR/audio-eq-cookbook/) | Uses the cookbook's bilinear-transform coefficients for low/high-pass, shelf, and peaking biquads in a local transposed-direct-form implementation. |
| `src/dsp/waveshape.h` | **Paper-derived** | Julian D. Parker, Vadim Zavalishin, and Efflam Le Bivic, “Reducing the Aliasing of Nonlinear Waveshaping Using Continuous-Time Convolution,” [DAFx-16](https://dafx.de/paper-archive/2016/dafxpapers/20-DAFx-16_paper_41-PN.pdf); curves after Martin Vicanek, [“Antiderivative Antialiasing for Memoryless Nonlinearities”](https://vicanek.de/articles/AADistortion.pdf) | Locally implements first-order antiderivative antialiasing (ADAA), including the small-difference fallback, for locally expressed hard, soft, and cubic curves. |
| `src/dsp/nonlinear_oversample.h`, `src/dsp/modules/fold.cpp` | **Locally generated / paper-derived** | Elliptic filter coefficients generated by the checked-in Wolfram Language listing `tools/design_nonlinear_filters.wls`; fold ADAA follows Parker, Zavalishin, and Le Bivic, DAFx-16 (cited above) | The 8x filter is a locally generated twelfth-order elliptic design stored as DC-normalized SOS, not copied from a library. The periodic fold antiderivative was derived and independently checked with Wolfram. Wolfram is an offline design tool, not a runtime dependency. |
| `src/dsp/bandlimited_osc.cpp`, `src/dsp/modules/vco.cpp` | **Concept-generic, informed by the existing band-limited oscillator research** | Standard Fourier series for saw and triangle waveforms; broader quality context from Vesa Välimäki, Jussi Pekonen, and Juhan Nam, “Perceptually informed synthesis of bandlimited classical waveforms using integrated polynomial interpolation,” *JASA* 131(1), 2012, [DOI 10.1121/1.3651227](https://doi.org/10.1121/1.3651227) ([research record](https://research.aalto.fi/en/publications/perceptually-informed-synthesis-of-bandlimited-classical-waveform/)) | Locally generates a shared bank of harmonic-limited classical-waveform tables from their standard Fourier series. Saw and triangle morph between adjacent Nyquist-safe tables; pulse subtracts phase-shifted saw tables and restores its analytic DC term. No source implementation or third-party table data was copied. |
| `src/dsp/modules/svf.cpp`, `src/dsp/modules/ladder.cpp`, and the SVF in `src/dsp/resonate.h` | **Paper/technical-note-derived** | Vadim Zavalishin, [*The Art of VA Filter Design*](https://www.native-instruments.com/fileadmin/ni_media/downloads/pdf/VAFilterDesign_2.0.0a.pdf), and Andrew Simper, [Cytomic trapezoidal SVF notes](https://www.cytomic.com/files/dsp/SvfLinearTrapOptimised.pdf) | Implements topology-preserving/trapezoidal-integrator filters: a TPT SVF and a four-stage ladder whose linear feedback is solved analytically. The resonator SVF is also adapted through stmlib/Rings as noted above. |
| `src/dsp/modules/euclid.cpp` | **Concept/paper-derived** | Godfried Toussaint, “The Euclidean Algorithm Generates Traditional Musical Rhythms,” 2005 ([paper](http://cgm.cs.mcgill.ca/~godfried/publications/banff.pdf)) | Uses Toussaint's Euclidean-rhythm framing but implements a constant-time modular bucket/Bresenham-style test; it is not a port of Bjorklund code. |
| `src/dsp/util.h` (`xorshift32`, used by `noise.cpp`, `dust.cpp`, `random.cpp`, `bern.cpp`, `turing.cpp`, `stepseq.cpp`, `grains.cpp`, and `src/dsp/resonate.h`) | **Paper-derived** | George Marsaglia, [“Xorshift RNGs,” *Journal of Statistical Software* 8(14), 2003](https://www.jstatsoft.org/article/view/v008i14) | A small xorshift32 generator (13/17/5 shifts) used for deterministic noise, probability, sequencing, grains, or excitation. |
| `src/rt/spsc.h` | **Implementation-influenced**, BSD-2-Clause and MIT | Cameron Desrochers' [readerwriterqueue](https://github.com/cameron314/readerwriterqueue) cached peer indices and PipeWire's acquire/release ring-buffer protocol | Combines independently written fixed-capacity SPSC storage with cached peer-index and memory-ordering patterns; this is not a verbatim port. Conservative software notices are retained in `THIRD_PARTY_NOTICES.md`. |
| `src/dsp/modules/bern.cpp` | **Concept-inspired** | Mutable Instruments [Branches](https://github.com/pichenettes/eurorack/tree/master/branches) Bernoulli gate routing | Adopts the probability-routed gate concept in the ratsnest module model. No Branches firmware code was copied. |
| `src/dsp/modules/turing.cpp` | **Concept-inspired** | Tom Whitwell's [Music Thing Modular Turing Machine](https://www.musicthing.co.uk/Turing-Machine/) | Adopts the looping, probabilistically mutated shift-register sequencer concept. No Turing Machine firmware code was copied. |
| `src/dsp/modules/fm.cpp` | **Concept-inspired** | John M. Chowning, "The Synthesis of Complex Audio Spectra by Means of Frequency Modulation," *JASA* 54(2), 1973, [DOI 10.1121/1.1912800](https://doi.org/10.1121/1.1912800) (technique patent US 4,018,121, granted Apr 1977, expired 1995 — see Patent status below) | Locally implements the published phase-modulation formula with an original operator-routing model, algorithm set, and envelope design. Operator waveforms (rectified/windowed/folded sine) implement the TX81Z/DX11/FS1R operator-waveform concept locally from the mathematics; keyboard level/rate scaling is a standard technique in an original octave-exponent form. No Yamaha/DX data, layouts, or code, no GPL/emulator code (e.g. Dexed/MSFA), and no trademarks; written locally from the published formula. |
| `src/dsp/modules/chorus.cpp` | **Concept-generic** | Standard multi-voice modulated-delay chorus (textbook DSP) | Written locally from first principles: interpolated short delay taps with decorrelated LFO phases summed with the dry signal. No third-party code; a decades-old effect with no live patent (factual note, not legal advice); no trademarks. |
| `src/dsp/modules/phaser.cpp` | **Concept-generic** | Standard swept first-order-allpass phaser; allpass coefficient from the project's existing bilinear/TPT filter lineage (see the Zavalishin / Bristow-Johnson rows above) | Written locally: a swept, optionally staggered allpass cascade with feedback and a dry/wet sum. No third-party code; a decades-old effect with no live patent (factual note, not legal advice); no trademarks. |
| Build and `src/engine/pipewire_out.cpp` | **External dependency**, MIT | [PipeWire](https://docs.pipewire.org/) `libpipewire-0.3` | The installed version is discovered through pkg-config and linked as `PkgConfig::PIPEWIRE`; PipeWire is not vendored. |
| `mstests` in `CMakeLists.txt` | **External dependency**, MIT | [doctest v2.4.11](https://github.com/doctest/doctest/tree/2.4.11) | CMake fetches the pinned test framework at test-build time and links `doctest::doctest`; it is not vendored. |
| `patches/lib/kit/` membrane voices (`kick.sub`, `tom.sub`, `snare.sub`) | **Paper-derived** | N. H. Fletcher and T. D. Rossing, *The Physics of Musical Instruments*, 2nd ed., Springer, 1998, ch. 18 (membranes) | The ideal circular-membrane Bessel-zero frequency ratios (1, 1.594, 2.136, 2.296, 2.653, 2.918) used as `modal { ratios: [...] }` constants. Published physical constants; no third-party code involved. |
| `patches/lib/kit/hat.sub`, `rim.sub` (metal-cluster ratios) | **Concept-inspired** | Roland TR-808 hi-hat oscillator bank (six-oscillator set ≈ 205.3/304.4/369.6/522.7/540/800 Hz, widely documented in service-manual analyses) | Normalized ratios (1, 1.483, 1.800, 2.546, 2.630, 3.897) voice `modal` banks as struck metal; `hat.sub` expands them by their odd harmonics (the 808's square-ish sources carry their partial stacks) into a 40-mode hash. No Roland schematics, firmware, or code reproduced. |
| `patches/lib/kit/hat_model.sub` (ring-mod carriers) | **Concept-inspired** | Émilie Gillet's [Mutable Instruments Plaits](https://github.com/pichenettes/eurorack/tree/master/plaits) AUX/FM hi-hat model (ring-modulated square-oscillator pairs into a metallic-noise voice) | Adopts the ring-modulated-pair topology and the published FM-hat carrier set (≈ 200/510/730 Hz × 7530/8075/10500 Hz), retuned/expanded to five spread pairs, in a `vco`+`math{mult}`+`noise` patch. Concept only — no Plaits firmware code was copied; the shared MIT notice is already retained in `THIRD_PARTY_NOTICES.md`. |

## Noise color extensions

Brown, blue, and violet noise are locally written integration/differencing
filters, informed by the spectral definitions in Julius O. Smith III's
[*Spectral Audio Signal Processing*, Spectrum Analysis of Noise](https://www.dsprelated.com/freebooks/sasp/Spectrum_Analysis_Noise.html).
This is concept/technical-note-derived, with no source code copied. Brown uses
a leaky integrator with a 10 Hz pole; blue differences the existing pink stream;
violet differences white. Dust uses the existing Marsaglia generator with a
locally written per-sample Bernoulli event/amplitude mapping.

The pre-existing three-pole pink implementation matches Paul Kellet's economy
filter published in [Robin Whittle's music-dsp archive](https://www.firstpr.com.au/dsp/pink-noise/).
Its original coefficients are unchanged. That archive says most or all
contributions were placed in the public domain, but a specific release statement
for this filter was not established here; its licensing provenance remains
unresolved. No stronger license claim is made by this update.

## Patent status (`fm` module)

Due-diligence research on the techniques used by `src/dsp/modules/fm.cpp`, as of
2026-07-18. **This is a factual expiry check, not a freedom-to-operate legal
opinion; it is not legal advice.** It covers the known *foundational* patents,
not an exhaustive live-patent database search across jurisdictions.

- **Phase-modulation core** — Chowning/Stanford US 4,018,121, granted April
  1977, **expired 1995** (17 years from grant extended to 20 years from the
  1975 filing under the GATT transition). This is the patent that covered
  digital FM/PM synthesis; its expiry is why FM became freely implementable in
  software. Phase modulation and Chowning's FM are mathematically equivalent, so
  the module's exact method falls under this now-expired patent.
- **Operator waveforms** (rectified/windowed/folded sine) — the multi-waveform
  operator debuts with the Yamaha TX81Z (1987); any corresponding Yamaha patent
  is a mid-1980s filing and **expired ~2004–2005** under the pre-1995 17-years-
  from-grant term. A waveform function is also non-patentable mathematics and
  decades-old prior art.
- **Keyboard level/rate scaling** — a 1980s DX-era Yamaha improvement (and a
  general synthesizer technique); same patent generation, **expired ~2005**.

Any patent still in force in 2026 must have been filed after ~2006; none of
these decades-old techniques can be validly claimed by such a patent. No live
patent covering the implemented techniques was found. Caveat: this is not an
FTO clearance — obtain patent counsel before any commercial distribution.
Sources: [FM synthesis (Wikipedia)](https://en.wikipedia.org/wiki/Frequency_modulation_synthesis),
[Yamaha TX81Z (Wikipedia)](https://en.wikipedia.org/wiki/Yamaha_TX81Z).

## Unresolved provenance

`src/dsp/oversample.h` contains the two first-order allpass coefficients
`0.117565365538` and `0.541450201656`. Repository history records them at their
initial introduction but does not name a design tool, paper, or upstream
implementation, and the attribution audit found no exact source match. They
are therefore documented as **source unknown**, not attributed speculatively.
Future replacement or regeneration should record its design method and license
before changing these values.

### Musical control workflow

The `transport`, `phrase`, and `scene` workflows are locally implemented,
concept-inspired by Pamela's Pro Workout's beat-based loops, Ornament & Crime's
CopierMaschine/Meta-Q, and blackbox's quantized scene launching. No firmware code
from these products was copied. Sources consulted:

- [Pamela's Pro Workout manual](https://assets.busycircuits.com/docs/alm034-manual.pdf)
- [O&C v1.3 manual](https://ornament-and-cri.me/user-manual-v1_3/)
- [blackbox 3.0 manual](https://1010music.com/wp-content/uploads/2023/05/Blackbox-User-Manual-3.0-2023-05-04a.pdf)

Their interfaces are inspiration, not claims of feature parity or algorithmic
identity. `channel` meters sample peak and smoothed RMS, not standardized loudness.
