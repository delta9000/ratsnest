#include <doctest/doctest.h>

#include <algorithm>
#include <array>
#include <cmath>

#include "dsp/nonlinear_oversample.h"

using ms::dsp::NonlinearOversampler;

namespace {

template <int Rate>
std::array<float, 4096> identity_impulse() {
  NonlinearOversampler<Rate> oversampler;
  std::array<float, 4096> response{};
  for (std::size_t i = 0; i < response.size(); ++i) {
    const float x = i == 0 ? 1.0f : 0.0f;
    response[i] = oversampler.process(x, [](float v) { return v; });
  }
  return response;
}

template <int Rate>
void check_round_trip_contract() {
  NonlinearOversampler<Rate> oversampler;

  for (int i = 0; i < 32; ++i)
    CHECK(oversampler.process(0.0f, [](float v) { return v; }) == 0.0f);

  for (int i = 0; i < 2048; ++i)
    (void)oversampler.process(0.25f, [](float v) { return v; });
  CHECK(oversampler.process(0.25f, [](float v) { return v; }) ==
        doctest::Approx(0.25f).epsilon(2.0e-5));

  oversampler.reset();
  const auto response = identity_impulse<Rate>();
  const auto peak = std::max_element(
      response.begin(), response.end(),
      [](float a, float b) { return std::abs(a) < std::abs(b); });
  CHECK(std::distance(response.begin(), peak) == 4);
  CHECK(std::isfinite(*peak));
  CHECK(std::abs(response.back()) < 1.0e-12f);

  oversampler.reset();
  const float first = oversampler.process(1.0f, [](float v) { return v; });
  oversampler.reset();
  CHECK(oversampler.process(1.0f, [](float v) { return v; }) == first);
}

}  // namespace

TEST_CASE("nonlinear oversample: 8x round trip has four-sample peak") {
  check_round_trip_contract<8>();
}
