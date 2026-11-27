#pragma once
#include <cstddef>

namespace ms::io {

// Programme loudness of a stereo take, measured offline (never RT).
// ITU-R BS.1770-4: K-weighting, 400 ms blocks on a 100 ms hop, absolute gate
// -70 LUFS, relative gate -10 LU. EBU Tech 3342 loudness range: 3 s
// short-term windows on a 100 ms hop, gates -70 LUFS and -20 LU, the spread
// between the 10th and 95th percentiles. True peak: the 4x interpolator the
// limiter uses (dsp/mastering.h). A take shorter than one block, or below the
// absolute gate throughout, reads -inf loudness and 0 range.
struct Loudness {
  double integrated = 0.0;  // LUFS
  double range = 0.0;       // LU
  double true_peak = 0.0;   // dBTP
};

Loudness measure_loudness(const float* l, const float* r, size_t frames, double rate);

}  // namespace ms::io
