#pragma once
// Minimum-phase band-limited step (minBLEP), after Brandt, "Hard Sync Without
// Aliasing" (ICMC 2001); docs/attribution.md. A naive waveform's step of
// height h, `t` samples ago, is band-limited by adding h * residual(t): the
// residual is the minimum-phase band-limited step minus the ideal step, so it
// starts at -1 and settles to 0 within kLength samples. Minimum phase makes it
// causal: the correction starts at the step, with no lookahead or latency.
//
// The table is built once (first use, control thread) and only read after.
#include <array>
#include <cmath>
#include <cstddef>

namespace ms::dsp {

class MinBlep {
 public:
  static constexpr int kLength = 32;  // samples until the residual is 0
  static constexpr int kOversample = 64;

  static const MinBlep& instance();

  // Residual at t samples after the step (t >= 0); 0 from kLength on.
  float residual(float t) const {
    const float i = t * kOversample;
    if (!(i >= 0.0f)) return -1.0f;
    const auto k = static_cast<std::size_t>(i);
    if (k + 1 >= kTable) return 0.0f;
    const float f = i - static_cast<float>(k);
    return table_[k] + f * (table_[k + 1] - table_[k]);
  }

 private:
  static constexpr std::size_t kTable = kLength * kOversample + 1;
  MinBlep();
  std::array<float, kTable> table_{};
};

// A causal ring of pending minBLEP corrections, one per oscillator.
class BlepRing {
 public:
  // Adds a step of height h that happened t samples before the next value
  // `next()` returns (0 <= t < 1).
  void step(float h, float t) {
    const MinBlep& mb = MinBlep::instance();
    for (int k = 0; k < MinBlep::kLength; ++k)
      buf_[(pos_ + static_cast<std::size_t>(k)) % kSize] +=
          h * mb.residual(t + static_cast<float>(k));
  }
  // The correction for this sample; advances the ring.
  float next() {
    const float v = buf_[pos_];
    buf_[pos_] = 0.0f;
    pos_ = (pos_ + 1) % kSize;
    return v;
  }
  void clear() { buf_.fill(0.0f); pos_ = 0; }

 private:
  static constexpr std::size_t kSize = MinBlep::kLength;
  std::array<float, kSize> buf_{};
  std::size_t pos_ = 0;
};

}  // namespace ms::dsp
