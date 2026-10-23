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

  // Table choice for one frequency. It depends only on frequency and rate, so
  // a caller holding its pitch still can compute it once and reuse it.
  struct Band {
    std::size_t level = 0;
    float mix = 1.0f;          // crossfade weight of `level` over `level - 1`
    bool blend_lower = false;  // false: read `level` alone
  };
  Band band(float frequency, float sample_rate) const;

  float saw(float phase, float frequency, float sample_rate) const;
  float triangle(float phase, float frequency, float sample_rate) const;
  float saw(float phase, const Band& band) const { return read(saw_, phase, band); }
  float triangle(float phase, const Band& band) const {
    return read(triangle_, phase, band);
  }

 private:
  static constexpr std::size_t kTableSize = 2048;
  // A real-valued N-point table can represent bins 1..N/2-1. Summing higher
  // Fourier partials here would fold them into the table before playback.
  static constexpr int kMaxHarmonics = static_cast<int>(kTableSize / 2) - 1;

  BandlimitedOsc();
  float read(const std::vector<float>& tables, float phase, const Band& band) const;

  std::vector<int> ceilings_;
  std::array<std::uint16_t, kMaxHarmonics + 1> level_for_harmonics_{};
  std::vector<float> saw_;
  std::vector<float> triangle_;
};

}  // namespace ms::dsp
