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
| `src/dsp/modules/compressor.cpp` | **Paper-derived** | Dimitrios Giannoulis, Michael Massberg, and Joshua D. Reiss, “Digital Dynamic Range Compressor Design—A Tutorial and Analysis,” *JAES* 60(6), 2012 ([paper](https://eecs.qmul.ac.uk/~josh/documents/2012/GiannoulisMassbergReiss-dynamicrangecompression-JAES2012.pdf)) | Locally implements the log-domain soft-knee static curve and attack/release gain-reduction ballistics. |
| `src/dsp/biquad.h`, `src/dsp/modules/eq.cpp` | **Paper/technical-note-derived** | Robert Bristow-Johnson, [W3C Audio EQ Cookbook](https://www.w3.org/TR/audio-eq-cookbook/) | Uses the cookbook's bilinear-transform coefficients for low/high-pass, shelf, and peaking biquads in a local transposed-direct-form implementation. |
| `src/dsp/waveshape.h` | **Paper-derived** | Julian D. Parker, Vadim Zavalishin, and Efflam Le Bivic, “Reducing the Aliasing of Nonlinear Waveshaping Using Continuous-Time Convolution,” [DAFx-16](https://dafx.de/paper-archive/2016/dafxpapers/20-DAFx-16_paper_41-PN.pdf); curves after Martin Vicanek, [“Antiderivative Antialiasing for Memoryless Nonlinearities”](https://vicanek.de/articles/AADistortion.pdf) | Locally implements first-order antiderivative antialiasing (ADAA), including the small-difference fallback, for locally expressed hard, soft, and cubic curves. |
| `src/dsp/modules/vco.cpp` | **Paper-derived family** | Vesa Välimäki, Jussi Pekonen, and Juhan Nam, “Perceptually informed synthesis of bandlimited classical waveforms using integrated polynomial interpolation,” *JASA* 131(1), 2012, [DOI 10.1121/1.3651227](https://doi.org/10.1121/1.3651227) ([research record](https://research.aalto.fi/en/publications/perceptually-informed-synthesis-of-bandlimited-classical-waveform/)) | Uses a compact two-sample polyBLEP correction for saw and pulse discontinuities. The formula was written locally; no source implementation was copied. |
| `src/dsp/modules/svf.cpp`, `src/dsp/modules/ladder.cpp`, and the SVF in `src/dsp/resonate.h` | **Paper/technical-note-derived** | Vadim Zavalishin, [*The Art of VA Filter Design*](https://www.native-instruments.com/fileadmin/ni_media/downloads/pdf/VAFilterDesign_2.0.0a.pdf), and Andrew Simper, [Cytomic trapezoidal SVF notes](https://www.cytomic.com/files/dsp/SvfLinearTrapOptimised.pdf) | Implements topology-preserving/trapezoidal-integrator filters: a TPT SVF and a four-stage ladder whose linear feedback is solved analytically. The resonator SVF is also adapted through stmlib/Rings as noted above. |
| `src/dsp/modules/euclid.cpp` | **Concept/paper-derived** | Godfried Toussaint, “The Euclidean Algorithm Generates Traditional Musical Rhythms,” 2005 ([paper](http://cgm.cs.mcgill.ca/~godfried/publications/banff.pdf)) | Uses Toussaint's Euclidean-rhythm framing but implements a constant-time modular bucket/Bresenham-style test; it is not a port of Bjorklund code. |
| `src/dsp/modules/noise.cpp`, `random.cpp`, `bern.cpp`, `turing.cpp`, `stepseq.cpp`, `grains.cpp`, and `src/dsp/resonate.h` | **Paper-derived** | George Marsaglia, [“Xorshift RNGs,” *Journal of Statistical Software* 8(14), 2003](https://www.jstatsoft.org/article/view/v008i14) | Each site contains a small local xorshift32 generator used for deterministic noise, probability, sequencing, grains, or excitation. |
| `src/rt/spsc.h` | **Implementation-influenced**, BSD-2-Clause and MIT | Cameron Desrochers' [readerwriterqueue](https://github.com/cameron314/readerwriterqueue) cached peer indices and PipeWire's acquire/release ring-buffer protocol | Combines independently written fixed-capacity SPSC storage with cached peer-index and memory-ordering patterns; this is not a verbatim port. Conservative software notices are retained in `THIRD_PARTY_NOTICES.md`. |
| `src/dsp/modules/bern.cpp` | **Concept-inspired** | Mutable Instruments [Branches](https://github.com/pichenettes/eurorack/tree/master/branches) Bernoulli gate routing | Adopts the probability-routed gate concept in the ratsnest module model. No Branches firmware code was copied. |
| `src/dsp/modules/turing.cpp` | **Concept-inspired** | Tom Whitwell's [Music Thing Modular Turing Machine](https://www.musicthing.co.uk/Turing-Machine/) | Adopts the looping, probabilistically mutated shift-register sequencer concept. No Turing Machine firmware code was copied. |
| Build and `src/engine/pipewire_out.cpp` | **External dependency**, MIT | [PipeWire](https://docs.pipewire.org/) `libpipewire-0.3` | The installed version is discovered through pkg-config and linked as `PkgConfig::PIPEWIRE`; PipeWire is not vendored. |
| `mstests` in `CMakeLists.txt` | **External dependency**, MIT | [doctest v2.4.11](https://github.com/doctest/doctest/tree/2.4.11) | CMake fetches the pinned test framework at test-build time and links `doctest::doctest`; it is not vendored. |

## Unresolved provenance

`src/dsp/oversample.h` contains the two first-order allpass coefficients
`0.117565365538` and `0.541450201656`. Repository history records them at their
initial introduction but does not name a design tool, paper, or upstream
implementation, and the attribution audit found no exact source match. They
are therefore documented as **source unknown**, not attributed speculatively.
Future replacement or regeneration should record its design method and license
before changing these values.
