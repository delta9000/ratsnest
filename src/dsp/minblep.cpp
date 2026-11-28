#include "dsp/minblep.h"

#include <cmath>
#include <complex>
#include <numbers>
#include <vector>

namespace ms::dsp {
namespace {

using Cx = std::complex<double>;

// In-place iterative radix-2 FFT (inverse unscaled when inv).
void fft(std::vector<Cx>& a, bool inv) {
  const std::size_t n = a.size();
  for (std::size_t i = 1, j = 0; i < n; ++i) {
    std::size_t bit = n >> 1;
    for (; j & bit; bit >>= 1) j ^= bit;
    j ^= bit;
    if (i < j) std::swap(a[i], a[j]);
  }
  for (std::size_t len = 2; len <= n; len <<= 1) {
    const double ang = 2.0 * std::numbers::pi / static_cast<double>(len) * (inv ? 1.0 : -1.0);
    const Cx wl(std::cos(ang), std::sin(ang));
    for (std::size_t i = 0; i < n; i += len) {
      Cx w(1.0);
      for (std::size_t k = 0; k < len / 2; ++k, w *= wl) {
        const Cx u = a[i + k], v = a[i + k + len / 2] * w;
        a[i + k] = u + v;
        a[i + k + len / 2] = u - v;
      }
    }
  }
}

}  // namespace

const MinBlep& MinBlep::instance() {
  static const MinBlep table;
  return table;
}

// Brandt's recipe: a Blackman-windowed sinc (cutoff 0.9 x Nyquist, kLength/2
// zero crossings each side, kOversample points per sample), made minimum
// phase through the real cepstrum, integrated to a step, minus the ideal step.
MinBlep::MinBlep() {
  const int n = static_cast<int>(kTable);
  const int half = n / 2;
  constexpr std::size_t kFft = 1u << 16;
  std::vector<Cx> a(kFft, Cx(0.0));
  for (int i = 0; i < n; ++i) {
    const double t = static_cast<double>(i - half) / kOversample;
    const double x = 0.9 * t;
    const double sinc = x == 0.0 ? 1.0 : std::sin(std::numbers::pi * x) / (std::numbers::pi * x);
    const double w = 0.42 - 0.5 * std::cos(2.0 * std::numbers::pi * i / (n - 1)) +
                     0.08 * std::cos(4.0 * std::numbers::pi * i / (n - 1));
    a[static_cast<std::size_t>(i)] = 0.9 * sinc * w;
  }
  fft(a, false);
  for (auto& v : a) v = Cx(std::log(std::abs(v) + 1e-20), 0.0);
  fft(a, true);
  for (auto& v : a) v /= static_cast<double>(kFft);  // real cepstrum
  for (std::size_t i = 1; i < kFft / 2; ++i) a[i] *= 2.0;
  for (std::size_t i = kFft / 2 + 1; i < kFft; ++i) a[i] = 0.0;
  fft(a, false);
  for (auto& v : a) v = std::exp(v);
  fft(a, true);
  double sum = 0.0;
  std::vector<double> step(kTable);
  for (std::size_t i = 0; i < kTable; ++i) {
    sum += a[i].real() / static_cast<double>(kFft);
    step[i] = sum;
  }
  for (std::size_t i = 0; i < kTable; ++i) table_[i] = static_cast<float>(step[i] / sum - 1.0);
  table_[kTable - 1] = 0.0f;

}

}  // namespace ms::dsp
