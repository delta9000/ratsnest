#include "dsp/bandlimited_osc.h"

#include <algorithm>
#include <cmath>
#include <limits>
#include <numbers>

namespace ms::dsp {
namespace {

void normalize(float* table, std::size_t size) {
  float peak = 0.0f;
  for (std::size_t i = 0; i < size; ++i)
    peak = std::max(peak, std::abs(table[i]));
  if (peak <= 0.0f) return;
  const float gain = 1.0f / peak;
  for (std::size_t i = 0; i < size; ++i) table[i] *= gain;
}

}  // namespace

const BandlimitedOsc& BandlimitedOsc::instance() {
  static const BandlimitedOsc bank;
  return bank;
}

BandlimitedOsc::BandlimitedOsc() {
  for (int h = 1; h <= 64; ++h) ceilings_.push_back(h);
  constexpr double kSemitone = 1.0594630943592952646;
  while (ceilings_.back() < kMaxHarmonics) {
    int next = static_cast<int>(std::lround(
        static_cast<double>(ceilings_.back()) * kSemitone));
    if (next <= ceilings_.back()) next = ceilings_.back() + 1;
    ceilings_.push_back(std::min(next, kMaxHarmonics));
  }

  std::size_t level = 0;
  for (int h = 0; h <= kMaxHarmonics; ++h) {
    while (level + 1 < ceilings_.size() && ceilings_[level + 1] <= h) ++level;
    level_for_harmonics_[static_cast<std::size_t>(h)] =
        static_cast<std::uint16_t>(level);
  }

  const std::size_t total = ceilings_.size() * kTableSize;
  saw_.assign(total, 0.0f);
  triangle_.assign(total, 0.0f);
  const double pi = std::numbers::pi;

  for (std::size_t li = 0; li < ceilings_.size(); ++li) {
    float* saw = saw_.data() + li * kTableSize;
    float* triangle = triangle_.data() + li * kTableSize;
    const int ceiling = ceilings_[li];
    for (int h = 1; h <= ceiling; ++h) {
      const double step = 2.0 * pi * static_cast<double>(h) /
                          static_cast<double>(kTableSize);
      const double sd = std::sin(step);
      const double cd = std::cos(step);
      double s = 0.0;
      double c = 1.0;
      const double saw_gain = -2.0 / (pi * static_cast<double>(h));
      const double tri_gain = h % 2 == 1
                                  ? 8.0 / (pi * pi * static_cast<double>(h * h))
                                  : 0.0;
      for (std::size_t i = 0; i < kTableSize; ++i) {
        saw[i] += static_cast<float>(saw_gain * s);
        triangle[i] += static_cast<float>(tri_gain * c);
        const double next_s = s * cd + c * sd;
        c = c * cd - s * sd;
        s = next_s;
      }
    }
    normalize(saw, kTableSize);
    normalize(triangle, kTableSize);
  }
}

float BandlimitedOsc::sample(const std::vector<float>& tables, float phase,
                             float frequency, float sample_rate) const {
  int harmonics = kMaxHarmonics;
  if (std::isfinite(frequency) && frequency > 0.0f &&
      std::isfinite(sample_rate) && sample_rate > 0.0f) {
    const float limit = std::nextafter(0.5f * sample_rate / frequency, 0.0f);
    harmonics = std::clamp(static_cast<int>(limit), 1, kMaxHarmonics);
  }
  const std::size_t level = level_for_harmonics_[static_cast<std::size_t>(harmonics)];
  phase -= std::floor(phase);
  const float position = phase * static_cast<float>(kTableSize);
  const auto index = static_cast<std::size_t>(position);
  const float fraction = position - static_cast<float>(index);
  const std::size_t base = level * kTableSize;
  const float a = tables[base + index];
  const float b = tables[base + ((index + 1) & (kTableSize - 1))];
  return a + fraction * (b - a);
}

float BandlimitedOsc::saw(float phase, float frequency, float sample_rate) const {
  return sample(saw_, phase, frequency, sample_rate);
}

float BandlimitedOsc::triangle(float phase, float frequency,
                               float sample_rate) const {
  return sample(triangle_, phase, frequency, sample_rate);
}

}  // namespace ms::dsp
