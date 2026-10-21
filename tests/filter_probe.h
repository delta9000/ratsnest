#pragma once
// Measurement helpers shared by the svf and ladder tests: drive a resonant
// filter module sample by sample and read back pitch, level, and gain.
#include <algorithm>
#include <cmath>
#include <functional>
#include <numbers>
#include <vector>

namespace filter_probe {

// One filter sample: (input, sample index) -> chosen output.
using Step = std::function<float(float, int)>;

struct Tone {
  double hz = 0.0;    // zero-crossing frequency
  double peak = 0.0;  // absolute peak
};

// Ping the filter with a tiny impulse, let it settle for `settle` seconds,
// then measure one second of whatever it sustains on its own.
inline Tone ring(const Step& step, double rate, double settle = 2.0) {
  const int start = static_cast<int>(settle * rate);
  const int end = start + static_cast<int>(rate);
  Tone tone;
  float prev = 0.0f;
  double first = -1.0, last = -1.0;
  int cycles = 0;
  for (int i = 0; i < end; ++i) {
    const float y = step(i == 0 ? 0.01f : 0.0f, i);
    if (i > start) {
      tone.peak = std::max(tone.peak, static_cast<double>(std::abs(y)));
      if (prev < 0.0f && y >= 0.0f) {
        const double t = (i - 1) + static_cast<double>(prev / (prev - y));
        if (first < 0.0) first = t;
        else { last = t; ++cycles; }
      }
    }
    prev = y;
  }
  if (cycles > 0) tone.hz = cycles * rate / (last - first);
  return tone;
}

inline double cents(double hz, double target) { return 1200.0 * std::log2(hz / target); }

// Gain of the component at `hz` (sine-fit over one second after warmup), so a
// self-oscillation at another frequency does not count.
inline double sine_gain(const Step& step, double rate, double hz, float amp) {
  const int warmup = static_cast<int>(rate), count = static_cast<int>(rate);
  double s = 0.0, c = 0.0;
  for (int i = 0; i < warmup + count; ++i) {
    const double phase = 2.0 * std::numbers::pi * hz * i / rate;
    const float y = step(amp * static_cast<float>(std::sin(phase)), i);
    if (i >= warmup) {
      s += y * std::sin(phase);
      c += y * std::cos(phase);
    }
  }
  return 2.0 * std::hypot(s, c) / count / amp;
}

}  // namespace filter_probe
