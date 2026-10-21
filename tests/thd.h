#pragma once
#include <cmath>
#include <numbers>
#include <vector>

// THD (ratio, not %) of `x` at fundamental `hz`: harmonics 2..20 over the
// fundamental, each by a direct DFT. Pass a whole number of periods.
inline double thd(const std::vector<float>& x, double hz, double rate) {
  auto mag = [&](double f) {
    double re = 0.0, im = 0.0;
    for (std::size_t n = 0; n < x.size(); ++n) {
      const double ph = 2.0 * std::numbers::pi * f * static_cast<double>(n) / rate;
      re += x[n] * std::cos(ph);
      im += x[n] * std::sin(ph);
    }
    return std::hypot(re, im);
  };
  double h = 0.0;
  for (int k = 2; k <= 20; ++k) h += std::pow(mag(hz * k), 2);
  return std::sqrt(h) / mag(hz);
}
