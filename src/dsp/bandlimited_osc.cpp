#include "dsp/bandlimited_osc.h"

#include <algorithm>
#include <cmath>
#include <limits>
#include <numbers>

namespace ms::dsp {

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

  // Accumulate every partial once, snapshotting only at requested ceilings.
  // Keeping the standard Fourier coefficients unchanged across snapshots is
  // crucial: normalizing each level independently causes audible gain steps.
  std::vector<double> saw_acc(kTableSize, 0.0);
  std::vector<double> triangle_acc(kTableSize, 0.0);
  std::size_t build_level = 0;
  for (int h = 1; h <= kMaxHarmonics; ++h) {
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
      saw_acc[i] += saw_gain * s;
      triangle_acc[i] += tri_gain * c;
      const double next_s = s * cd + c * sd;
      c = c * cd - s * sd;
      s = next_s;
    }

    if (h != ceilings_[build_level]) continue;
    const std::size_t base = build_level * kTableSize;
    for (std::size_t i = 0; i < kTableSize; ++i) {
      saw_[base + i] = static_cast<float>(saw_acc[i]);
      triangle_[base + i] = static_cast<float>(triangle_acc[i]);
    }
    ++build_level;
  }
}

float BandlimitedOsc::sample(const std::vector<float>& tables, float phase,
                             float frequency, float sample_rate) const {
  int harmonics = kMaxHarmonics;
  float harmonic_capacity = std::numeric_limits<float>::infinity();
  if (std::isfinite(frequency) && frequency > 0.0f &&
      std::isfinite(sample_rate) && sample_rate > 0.0f) {
    harmonic_capacity =
        std::nextafter(0.5f * sample_rate / frequency, 0.0f);
    harmonics = !std::isfinite(harmonic_capacity) ||
                        harmonic_capacity >= static_cast<float>(kMaxHarmonics)
                    ? kMaxHarmonics
                    : std::clamp(static_cast<int>(harmonic_capacity), 1,
                                 kMaxHarmonics);
  }
  const std::size_t level = level_for_harmonics_[static_cast<std::size_t>(harmonics)];
  phase = std::isfinite(phase) ? phase - std::floor(phase) : 0.0f;
  const float position = phase * static_cast<float>(kTableSize);
  const auto index = static_cast<std::size_t>(position);
  const float fraction = position - static_cast<float>(index);
  const auto read = [&](std::size_t table_level) {
    const std::size_t base = table_level * kTableSize;
    const float a = tables[base + index];
    const float b = tables[base + ((index + 1) & (kTableSize - 1))];
    return a + fraction * (b - a);
  };

  const float selected = read(level);
  if (level == 0 || !std::isfinite(harmonic_capacity)) return selected;

  // Fade a newly safe table in over the interval before the next table can
  // become safe. At the next threshold its lower endpoint is therefore
  // exactly the preceding table, so pitch sweeps have no waveform jump.
  const float ceiling = static_cast<float>(ceilings_[level]);
  const float next_ceiling =
      level + 1 < ceilings_.size()
          ? static_cast<float>(ceilings_[level + 1])
          : ceiling + 1.0f;
  const float mix =
      std::clamp((harmonic_capacity - ceiling) / (next_ceiling - ceiling),
                 0.0f, 1.0f);
  if (mix >= 1.0f) return selected;
  const float lower = read(level - 1);
  return lower + mix * (selected - lower);
}

float BandlimitedOsc::saw(float phase, float frequency, float sample_rate) const {
  return sample(saw_, phase, frequency, sample_rate);
}

float BandlimitedOsc::triangle(float phase, float frequency,
                               float sample_rate) const {
  return sample(triangle_, phase, frequency, sample_rate);
}

}  // namespace ms::dsp
