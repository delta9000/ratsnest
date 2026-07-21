#pragma once

#include <algorithm>
#include <cmath>
#include <cstddef>

namespace ms::engine {

inline float clamp_device_sample(float x) {
  if (!std::isfinite(x)) return 0.0f;
  return std::clamp(x, -1.0f, 1.0f);
}

inline void clamp_device_buffer(float* left, float* right, std::size_t frames) {
  for (std::size_t i = 0; i < frames; ++i) {
    left[i] = clamp_device_sample(left[i]);
    right[i] = clamp_device_sample(right[i]);
  }
}

}  // namespace ms::engine
