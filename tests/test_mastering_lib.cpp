#include <doctest/doctest.h>
#include <cmath>
#include "dsp/mastering.h"

using namespace ms::dsp;

TEST_CASE("mastering_lib: dB and linear conversions") {
  CHECK(db_to_linear(0.0f) == doctest::Approx(1.0f));
  CHECK(db_to_linear(6.0205999f) == doctest::Approx(2.0f));
  CHECK(db_to_linear(-6.0205999f) == doctest::Approx(0.5f));
  
  CHECK(linear_to_db(1.0f) == doctest::Approx(0.0f));
  CHECK(linear_to_db(2.0f) == doctest::Approx(6.0205999f));
  CHECK(linear_to_db(0.5f) == doctest::Approx(-6.0205999f));
  
  // Under floor handling
  CHECK(linear_to_db(0.0f) == -240.0f);
  CHECK(linear_to_db(-1.0f) == -240.0f);
}

TEST_CASE("mastering_lib: sanitization") {
  CHECK(sanitize(5.0f, 0.0f) == 5.0f);
  CHECK(sanitize(NAN, 3.0f) == 3.0f);
  CHECK(sanitize(INFINITY, -1.0f) == -1.0f);
  CHECK(sanitize(-INFINITY, 0.0f) == 0.0f);

  CHECK(sanitize_clamp(5.0f, 1.0f, 4.0f, 0.0f) == 4.0f);
  CHECK(sanitize_clamp(0.5f, 1.0f, 4.0f, 0.0f) == 1.0f);
  CHECK(sanitize_clamp(NAN, 1.0f, 4.0f, -5.0f) == -5.0f);
}

TEST_CASE("mastering_lib: flush_denormal") {
  CHECK(flush_denormal(0.1f) == 0.1f);
  CHECK(flush_denormal(1e-19f) == 1e-19f);
  CHECK(flush_denormal(1e-21f) == 0.0f);
  CHECK(flush_denormal(-1e-21f) == 0.0f);
}

TEST_CASE("mastering_lib: OnePole parameter smoothing") {
  OnePole op;
  op.init(100.0f);
  CHECK(op.value() == 100.0f);

  // Set coefficient for a time constant at a sample rate
  op.set_time(0.020f, 48000.0f); // 20ms at 48kHz
  
  // Step response: step from 100 to 200
  float current = 100.0f;
  for (int i = 0; i < 10000; ++i) {
    current = op.process(200.0f);
  }
  // After many steps, it should be very close to the target
  CHECK(std::abs(current - 200.0f) < 1e-2f);

  // Verify that setting non-positive time results in instantaneous jump
  op.set_time(0.0f, 48000.0f);
  CHECK(op.process(300.0f) == 300.0f);
}
