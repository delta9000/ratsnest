#pragma once
// Small helpers shared by many modules. Header-only, RT-safe: no allocation,
// no locks, no I/O.
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <numbers>
#include "dsp/module.h"

namespace ms::dsp {

// NaN-proof clamp: NaN collapses to lo.
inline float clamp_nan(float x, float lo, float hi) {
  if (!(x >= lo)) return lo;
  return x > hi ? hi : x;
}
inline double clamp_nan(double x, double lo, double hi) {
  if (!(x >= lo)) return lo;
  return x > hi ? hi : x;
}

// A control voltage rounded to an integer in [lo, hi]; non-finite -> fallback.
inline int round_clamp(float value, int lo, int hi, int fallback) {
  if (!std::isfinite(value)) return fallback;
  return static_cast<int>(std::lround(
      std::clamp(value, static_cast<float>(lo), static_cast<float>(hi))));
}

// A logic level as a gate signal.
inline float as_gate(bool high) { return high ? 1.0f : 0.0f; }

// Gate/trigger rising edge (spec §4: gates rise through 0.5). The previous
// sample is kept raw, so a NaN never counts as low.
struct RisingEdge {
  float prev = 0.0f;
  bool operator()(float v) {
    const bool rose = v > 0.5f && prev <= 0.5f;
    prev = v;
    return rose;
  }
};

// Slews out a step in a signal (switch selection, sampler loop seam). After
// jump(last_out, new_value, n) the output is the new signal plus an offset
// that starts at their difference and falls linearly to zero over n samples,
// so the output stays continuous. n < 1 is an instant cut; a non-finite
// step is dropped. Two floats of state; no allocation.
struct Declick {
  float offset = 0.0f, step = 0.0f;
  void jump(float from, float to, float samples) {
    const float d = from - to;
    offset = std::isfinite(d) ? d * static_cast<float>(samples >= 1.0f) : 0.0f;
    step = offset / std::max(samples, 1.0f);
  }
  float operator()(float x) {
    const float y = x + offset;
    offset -= std::copysign(std::fmin(std::fabs(step), std::fabs(offset)), offset);
    return y;
  }
};

// Envelope segment curve: the fraction of the way from start to target after
// progress x in [0, 1], indexed like the lin/exp/log/quad `curve` configs.
// `exp` is a normalized RC segment (1 - e^-kx) / (1 - e^-k): fast, then
// asymptotic, snapped onto its target at x = 1 so stage times keep meaning.
// Falls use k = ln 1000, so the stage time is the -60 dB time (T60); rises use
// k = ln 3, the analog attack that charges toward 1.5 and stops at 1.
// `quad` is x^2, the slow-start shape `exp` named before.
inline double env_curve(int curve, double x, bool rising) {
  switch (curve) {
    case 1: return rising ? 1.5 * (1.0 - std::exp(-1.0986122886681098 * x))
                          : (1.0 - std::exp(-6.907755278982137 * x)) / 0.999;
    case 2: return std::sqrt(x);
    case 3: return x * x;
    default: return x;
  }
}
// Per-sample progress through a segment of `seconds`. Double accumulation:
// float increments drop below half a ULP past 2^25 samples (~11.6 min at
// 48 kHz) and the envelope would freeze short of its target.
inline double env_increment(double seconds, double rate) {
  const double samples = std::max(seconds, 0.0) * rate;
  return samples <= 1.0 ? 1.0 : 1.0 / samples;
}

// Modulation LFO of the modfx (chorus, phaser), indexed like their
// tri/sine `shape` configs. ph in [0,1) -> [-1,1].
inline float mod_lfo(int shape, float ph) {
  if (shape == 1) return std::sin(2.0f * std::numbers::pi_v<float> * ph);
  return 1.0f - 4.0f * std::fabs(ph - 0.5f);  // triangle
}

// Marsaglia xorshift32; see docs/attribution.md. Zero state is forbidden.
inline std::uint32_t xorshift32(std::uint32_t& s) {
  s ^= s << 13;
  s ^= s >> 17;
  s ^= s << 5;
  return s;
}
// A draw scaled to [0, 1).
inline float unit_float(std::uint32_t x) {
  return static_cast<float>(x) * (1.0f / 4294967296.0f);
}

// The `seed` config as a nonzero xorshift state: |seed| saturated to uint32
// (the cast is UB out of range), Knuth-multiplied, zero remapped.
inline std::uint32_t cfg_seed(const Config& cfg) {
  double seed = cfg_num(cfg, "seed", 1.0);
  if (!std::isfinite(seed)) seed = 1.0;
  const std::uint32_t s =
      static_cast<std::uint32_t>(std::clamp(std::abs(seed), 0.0, 4294967295.0)) *
      2654435761u;
  return s ? s : 0x9E3779B9u;
}

}  // namespace ms::dsp
