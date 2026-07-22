#pragma once

#include <array>
#include <cmath>
#include <cstddef>
#include <utility>

namespace ms::dsp {
namespace nonlinear_detail {

struct SosCoefficients {
  float b0;
  float b1;
  float b2;
  float a1;
  float a2;
};

// Wolfram-generated, DC-normalized elliptic lowpasses. See
// tools/design_nonlinear_filters.wls. Each design has 0.001 dB passband ripple
// and 80 dB stopband attenuation. Cascaded SOS avoid the numerical fragility
// of a twelfth-order direct form.
inline constexpr std::array<SosCoefficients, 6> kSos8x{{
    {0.2533872483f, -0.4593270850f, 0.2533872483f,
     -1.5887756410f, 0.6362230525f},
    {0.3547386845f, -0.6383852393f, 0.3547386845f,
     -1.6412004946f, 0.7122926243f},
    {0.4288128444f, -0.7560916246f, 0.4288128444f,
     -1.7094625657f, 0.8109966300f},
    {0.3770862690f, -0.6284222341f, 0.3770862690f,
     -1.7657882420f, 0.8915385459f},
    {0.2135441584f, -0.2862038401f, 0.2135441584f,
     -1.8054427681f, 0.9463272449f},
    {0.0612436958f, 0.0261964652f, 0.0612436958f,
     -1.8352358540f, 0.9839197108f},
}};

template <int Rate>
constexpr const auto& coefficients() {
  static_assert(Rate == 8);
  return kSos8x;
}

class Biquad {
 public:
  float process(float x, const SosCoefficients& c) {
    const float y = c.b0 * x + z1_;
    z1_ = c.b1 * x - c.a1 * y + z2_;
    z2_ = c.b2 * x - c.a2 * y;
    if (std::abs(z1_) < 1.0e-20f) z1_ = 0.0f;
    if (std::abs(z2_) < 1.0e-20f) z2_ = 0.0f;
    return y;
  }

  void reset() {
    z1_ = 0.0f;
    z2_ = 0.0f;
  }

 private:
  float z1_ = 0.0f;
  float z2_ = 0.0f;
};

template <int Rate>
class EllipticCascade {
 public:
  float process(float x) {
    const auto& sos = coefficients<Rate>();
    for (std::size_t i = 0; i < sections_.size(); ++i)
      x = sections_[i].process(x, sos[i]);
    return x;
  }

  void reset() {
    for (auto& section : sections_) section.reset();
  }

 private:
  std::array<Biquad, 6> sections_{};
};

}  // namespace nonlinear_detail

template <int Rate>
class NonlinearOversampler {
 public:
  static_assert(Rate == 8);

  void reset() {
    interpolator_.reset();
    reconstructor_.reset();
  }

  template <typename Shape>
  float process(float x, Shape&& shape) {
    return process_phase<0>(x, std::forward<Shape>(shape));
  }

  template <int OutputPhase, typename Shape>
  float process_phase(float x, Shape&& shape) {
    static_assert(OutputPhase >= 0 && OutputPhase < Rate);
    float output = 0.0f;
    for (int phase = 0; phase < Rate; ++phase) {
      const float sparse = phase == 0 ? x * static_cast<float>(Rate) : 0.0f;
      const float high_rate = interpolator_.process(sparse);
      const float filtered = reconstructor_.process(shape(high_rate));
      if (phase == OutputPhase) output = filtered;
    }
    return output;
  }

 private:
  nonlinear_detail::EllipticCascade<Rate> interpolator_{};
  nonlinear_detail::EllipticCascade<Rate> reconstructor_{};
};

}  // namespace ms::dsp
