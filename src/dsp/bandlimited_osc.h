#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <vector>

namespace ms::dsp {

// Shared immutable Fourier-series tables for the ordinary VCO. Construction
// is deliberately confined to VCO::init(); reads are lock- and allocation-free.
class BandlimitedOsc {
 public:
  static const BandlimitedOsc& instance();

  float saw(float phase, float frequency, float sample_rate) const;
  float triangle(float phase, float frequency, float sample_rate) const;

 private:
  static constexpr std::size_t kTableSize = 2048;
  static constexpr int kMaxHarmonics = 2048;

  BandlimitedOsc();
  float sample(const std::vector<float>& tables, float phase, float frequency,
               float sample_rate) const;

  std::vector<int> ceilings_;
  std::array<std::uint16_t, kMaxHarmonics + 1> level_for_harmonics_{};
  std::vector<float> saw_;
  std::vector<float> triangle_;
};

}  // namespace ms::dsp
