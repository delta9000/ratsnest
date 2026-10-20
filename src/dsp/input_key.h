#pragma once
#include <array>
#include <bit>
#include <cstddef>
#include <cstdint>

namespace ms::dsp {

// Bit-exact memo key for coefficients derived from module inputs. Most
// cutoffs, times and levels sit still for thousands of samples, so a
// process() that recomputes tan/exp2/pow from them every sample can skip the
// libm call while the inputs' bit patterns are unchanged. Comparing bits, not
// values, makes the cached result exactly what a recompute would give (NaN
// payloads and signed zeros included), so renders stay bit-identical.
template <std::size_t N>
class InputKey {
 public:
  // True on first use and whenever any input differs from the previous call.
  bool changed(const std::array<float, N>& v) {
    bool diff = !valid_;
    for (std::size_t i = 0; i < N; ++i) {
      const auto b = std::bit_cast<std::uint32_t>(v[i]);
      diff |= b != bits_[i];
      bits_[i] = b;
    }
    valid_ = true;
    return diff;
  }

 private:
  std::array<std::uint32_t, N> bits_{};
  bool valid_ = false;
};

}  // namespace ms::dsp
