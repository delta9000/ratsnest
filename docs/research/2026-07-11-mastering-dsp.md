# Mastering DSP research

Research date: 2026-07-11. Scope: stereo `dcblock`, `compressor`, `eq`,
`clipper`, `limiter`, and `stereo` modules. Metering and offline loudness
analysis are recorded as follow-up tooling, not audio processors.

## Primary references

- Giannoulis, Massberg, Reiss, “Digital Dynamic Range Compressor Design — A
  Tutorial and Analysis,” JAES 60(6), 2012:
  https://eecs.qmul.ac.uk/~josh/documents/2012/GiannoulisMassbergReiss-dynamicrangecompression-JAES2012.pdf
- W3C Audio Working Group, “Audio EQ Cookbook,” adapted from Robert
  Bristow-Johnson: https://www.w3.org/TR/audio-eq-cookbook/
- ITU-R BS.1770-5, “Algorithms to measure audio programme loudness and
  true-peak audio level”: https://www.itu.int/rec/R-REC-BS.1770-5-202311-I
- EBU Tech 3341 v4, “‘EBU Mode’ metering”:
  https://tech.ebu.ch/docs/tech/tech3341.pdf
- Mapes-Riordan and Leach, “The Design of a Digital Signal Peak Limiter for
  Audio Signal Processing,” JAES 36(7/8), 1988:
  https://secure.aes.org/forum/pubs/journal/?elib=5139
- Parker, Zavalishin, Le Bivic, “Reducing the Aliasing of Nonlinear
  Waveshaping Using Continuous-Time Convolution,” DAFx-16:
  https://dafx.de/paper-archive/2016/dafxpapers/20-DAFx-16_paper_41-PN.pdf
- “Oversampling for Nonlinear Waveshaping: Choosing the Right Filters,” JAES:
  https://secure.aes.org/forum/pubs/journal/?elib=20485
- Wickelmaier and Choisel, AES 118th Convention paper on M/S-ratio width:
  https://www.mathpsy.uni-tuebingen.de/wickelmaier/pubs/WickelmaierChoisel2005AES.pdf

## Conclusions

### Dynamics

Use a feed-forward compressor in the logarithmic domain. Detect the maximum
absolute L/R sidechain sample so gain reduction is stereo-linked. Use the
Giannoulis soft-knee static curve and separate attack/release smoothing of gain
reduction, not raw waveform amplitude. Peak detection is predictable and
appropriate for the initial mastering/bus module; RMS detection can be added
later only if a real use case appears.

The limiter is a separate processor because lookahead changes latency and
allocation. A fixed delay line is allocated in `init()` from a config-time
lookahead. The detector sees the undelayed stereo peak and controls delayed
audio. Version one guarantees sample-peak containment. It must not claim true
peak: BS.1770 true peak requires specified oversampled reconstruction and is a
later limiter mode plus analysis feature.

### Equalization

Use RBJ bilinear-transform biquads for high-pass, low/high shelves, and two
peaking bands. Transposed Direct Form II requires two states per channel per
section. Coefficients are shared by L/R, keeping response linked. Recomputing
coefficients every sample is acceptable for six sections but parameter
smoothing is mandatory to avoid zipper noise; smooth parameters first, then
derive coefficients. All frequencies clamp below Nyquist and Q/gain are
bounded before trigonometric/exponential operations.

### Nonlinearity and oversampling

Hard clipping maximizes high-frequency generation and aliasing. Provide soft
and hard modes but process at fixed 4x oversampling, with paired interpolation
and decimation low-pass filters. Fixed factor/config keeps process state small
and avoids RT allocation. First implementation should use a well-tested
polyphase half-band cascade (2x twice) rather than inventing a variable-rate
resampler. Latency is fixed and documented.

### DC and stereo

DC removal is the one-pole high-pass `y[n] = x[n] - x[n-1] + R*y[n-1]`, with
`R = exp(-2*pi*cutoff/rate)`, independently stateful per channel. Cutoff is
smoothed and clamped to a mastering-safe range.

Width is normalized mid/side: `M=(L+R)/sqrt(2)`, `S=(L-R)/sqrt(2)`, scale S,
then decode with the same normalization. Width zero is mono; one is identity.
Correlation is an exponentially windowed probe output, not control of the
audio. Bass-mono crossover is deferred: it requires crossover phase design
and is not needed to validate the core stereo utility.

### Metering boundary

`meter` should follow `scope`: it observes without altering the graph. Offline
integrated LUFS, loudness range, normalization, and final true-peak scanning
belong in `modsynth analyze`, because they require programme history and often
two passes. EBU Tech 3341 defines 400 ms momentary and 3 s short-term windows;
ITU-R BS.1770 defines K-weighting, channel weighting, gating, and true-peak
reconstruction. These standards should be implemented literally and verified
with published conformance material in a separate project.
