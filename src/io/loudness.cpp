#include "io/loudness.h"
#include <algorithm>
#include <cmath>
#include <limits>
#include <numbers>
#include <vector>
#include "dsp/mastering.h"

// Paper-derived: the K-weighting pre-filter and RLB high-pass of ITU-R
// BS.1770-4 Annex 1, as the analog prototypes (f0, gain, Q) that make the
// filters rate-independent, and the gating of EBU Tech 3342 for the range.
// See docs/attribution.md.

namespace ms::io {
namespace {

struct Biquad {
  double b0, b1, b2, a1, a2, z1 = 0.0, z2 = 0.0;
  double step(double x) {
    const double y = b0 * x + z1;
    z1 = b1 * x - a1 * y + z2;
    z2 = b2 * x - a2 * y;
    return y;
  }
};

// Stage 1: the head's acoustic high shelf (+4 dB above ~1.7 kHz).
Biquad shelf(double rate) {
  const double k = std::tan(std::numbers::pi * 1681.974450955533 / rate);
  const double q = 0.7071752369554196;
  const double vh = std::pow(10.0, 3.999843853973347 / 20.0);
  const double vb = std::pow(vh, 0.4996667741545416);
  const double a0 = 1.0 + k / q + k * k;
  return {(vh + vb * k / q + k * k) / a0, 2.0 * (k * k - vh) / a0,
          (vh - vb * k / q + k * k) / a0, 2.0 * (k * k - 1.0) / a0,
          (1.0 - k / q + k * k) / a0};
}

// Stage 2: the RLB high-pass at ~38 Hz.
Biquad highpass(double rate) {
  const double k = std::tan(std::numbers::pi * 38.13547087602444 / rate);
  const double q = 0.5003270373238773;
  const double a0 = 1.0 + k / q + k * k;
  return {1.0, -2.0, 1.0, 2.0 * (k * k - 1.0) / a0, (1.0 - k / q + k * k) / a0};
}

double lufs(double mean_square) {
  return mean_square > 0.0 ? -0.691 + 10.0 * std::log10(mean_square)
                           : -std::numeric_limits<double>::infinity();
}

// Mean-square power (L + R) of every `len`-sample window starting on a
// multiple of `hop`, from per-hop sums of the K-weighted power.
std::vector<double> windows(const std::vector<double>& hops, size_t per, size_t hop) {
  std::vector<double> out;
  double sum = 0.0;
  for (size_t i = 0; i < hops.size(); ++i) {
    sum += hops[i];
    if (i >= per) sum -= hops[i - per];
    if (i + 1 >= per) out.push_back(std::max(0.0, sum) / static_cast<double>(per * hop));
  }
  return out;
}

// Mean power of the windows above both the absolute gate and `rel_lu` under
// the mean of those above the absolute gate; `kept` receives their loudness.
double gated_mean(const std::vector<double>& ms, double rel_lu, std::vector<double>* kept) {
  const double abs_gate = std::pow(10.0, (-70.0 + 0.691) / 10.0);
  double sum = 0.0;
  size_t n = 0;
  for (double m : ms)
    if (m > abs_gate) { sum += m; ++n; }
  if (n == 0) return 0.0;
  const double rel_gate = sum / static_cast<double>(n) * std::pow(10.0, rel_lu / 10.0);
  sum = 0.0;
  n = 0;
  for (double m : ms) {
    if (m <= abs_gate || m <= rel_gate) continue;
    sum += m;
    ++n;
    if (kept) kept->push_back(lufs(m));
  }
  return n ? sum / static_cast<double>(n) : 0.0;
}

double percentile(std::vector<double>& v, double p) {
  std::sort(v.begin(), v.end());
  const double pos = p * static_cast<double>(v.size() - 1);
  const auto lo = static_cast<size_t>(pos);
  const size_t hi = std::min(lo + 1, v.size() - 1);
  return v[lo] + (v[hi] - v[lo]) * (pos - static_cast<double>(lo));
}
}  // namespace

Loudness measure_loudness(const float* l, const float* r, size_t frames, double rate) {
  const auto hop = static_cast<size_t>(std::max(1.0, std::round(rate * 0.1)));
  Biquad fl[2] = {shelf(rate), highpass(rate)}, fr[2] = {shelf(rate), highpass(rate)};
  ms::dsp::TruePeak4x tp_l, tp_r;
  std::vector<double> hops;
  double acc = 0.0;
  float peak = 0.0f;
  for (size_t i = 0; i < frames; ++i) {
    const double yl = fl[1].step(fl[0].step(l[i]));
    const double yr = fr[1].step(fr[0].step(r[i]));
    acc += yl * yl + yr * yr;
    peak = std::max({peak, tp_l.push(l[i]), tp_r.push(r[i])});
    if ((i + 1) % hop == 0) { hops.push_back(acc); acc = 0.0; }
  }
  // Flush the interpolator so a crest in the last samples is still seen.
  for (int i = 0; i < ms::dsp::TruePeak4x::kLag; ++i)
    peak = std::max({peak, tp_l.push(0.0f), tp_r.push(0.0f)});

  Loudness out;
  out.true_peak = 20.0 * std::log10(std::max(static_cast<double>(peak), 1e-12));
  out.integrated = lufs(gated_mean(windows(hops, 4, hop), -10.0, nullptr));
  std::vector<double> st;
  gated_mean(windows(hops, 30, hop), -20.0, &st);
  if (st.size() > 1) out.range = percentile(st, 0.95) - percentile(st, 0.10);
  return out;
}

}  // namespace ms::io
