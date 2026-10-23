#include <doctest/doctest.h>
#include <cmath>
#include <array>
#include <numbers>
#include <vector>
#include "dsp/modules.h"

using namespace ms::dsp;

namespace {
std::unique_ptr<Module> make_noise(const char* color, double seed = 1.0, double rate = 48000.0) {
  const ModuleInfo* info = find_module("noise");
  REQUIRE(info != nullptr);
  auto m = info->create();
  Config cfg;
  ConfigValue c; c.str = color;
  cfg.push_back({"color", c});
  ConfigValue s; s.num = seed; s.is_num = true;
  cfg.push_back({"seed", s});
  m->init(rate, cfg);
  return m;
}

TEST_CASE("noise colors have their stated spectral slopes at different sample rates") {
  const std::array<const char*, 5> colors{"white", "pink", "brown", "blue", "violet"};
  const std::array<double, 5> slopes{0, -3, -6, 3, 6};
  constexpr int block = 8192;
  for (double rate : {44100.0, 48000.0, 96000.0}) {
    std::array<double, 4> white{};
    for (size_t c = 0; c < colors.size(); ++c) {
      CAPTURE(rate);
      CAPTURE(colors[c]);
      auto m = make_noise(colors[c], 7, rate);
      float out[1];
      for (int i = 0; i < block; ++i) m->process(nullptr, out);
      std::array<double, 4> power{};
      std::vector<double> samples(block);
      for (int segment = 0; segment < 16; ++segment) {
        for (int i = 0; i < block; ++i) {
          m->process(nullptr, out);
          REQUIRE(std::isfinite(out[0]));
          samples[static_cast<size_t>(i)] = out[0] *
              (0.5 - 0.5 * std::cos(2.0 * std::numbers::pi * i / block));
        }
        for (size_t band = 0; band < power.size(); ++band) {
          const double omega = 2.0 * std::numbers::pi * (64 << band) / block;
          const double coeff = 2.0 * std::cos(omega);
          double s1 = 0, s2 = 0;
          for (double x : samples) {
            const double s0 = x + coeff * s1 - s2;
            s2 = s1; s1 = s0;
          }
          power[band] += s1*s1 + s2*s2 - coeff*s1*s2;
        }
      }
      if (c == 0) white = power;
      // Same seed removes most estimator variance; compare adjacent octaves
      // against the white source, not raw noisy FFT bin magnitudes.
      for (size_t band = 1; band < power.size(); ++band) {
        const double slope = 10.0 * std::log10(
            (power[band] / white[band]) / (power[band-1] / white[band-1]));
        CAPTURE(band);
        CAPTURE(slope);
        CHECK(std::abs(slope - slopes[c]) < 0.8);
      }
    }
  }
}

TEST_CASE("every noise color survives reload sample-identically") {
  for (const char* color : {"white", "pink", "brown", "blue", "violet"}) {
    auto a = make_noise(color, 7);
    auto b = make_noise(color, 7);
    float oa[1], ob[1];
    for (int i = 0; i < 4096; ++i) a->process(nullptr, oa);
    b->copy_state_from(*a);
    for (int i = 0; i < 1024; ++i) {
      a->process(nullptr, oa); b->process(nullptr, ob);
      CHECK(oa[0] == ob[0]);
    }
  }
}
}  // namespace

TEST_CASE("noise is bounded, nonzero, and deterministic per seed") {
  auto a = make_noise("white", 7.0);
  auto b = make_noise("white", 7.0);
  auto c = make_noise("white", 8.0);
  float oa[1], ob[1], oc[1];
  bool differs_from_other_seed = false;
  double sumsq = 0.0;
  for (int i = 0; i < 4096; ++i) {
    a->process(nullptr, oa);
    b->process(nullptr, ob);
    c->process(nullptr, oc);
    CHECK(std::abs(oa[0]) <= 1.0f);
    CHECK(oa[0] == ob[0]);  // same seed, same stream
    if (oa[0] != oc[0]) differs_from_other_seed = true;
    sumsq += static_cast<double>(oa[0]) * oa[0];
  }
  CHECK(differs_from_other_seed);
  CHECK(std::sqrt(sumsq / 4096.0) > 0.1);  // actually noisy, not near-silent
}

TEST_CASE("pink noise is smoother than white noise") {
  auto white = make_noise("white");
  auto pink = make_noise("pink");
  float ow[1], op[1];
  float prev_w = 0.0f, prev_p = 0.0f;
  double dw = 0.0, dp = 0.0;
  for (int i = 0; i < 8192; ++i) {
    white->process(nullptr, ow);
    pink->process(nullptr, op);
    if (i > 0) {
      dw += std::abs(ow[0] - prev_w);
      dp += std::abs(op[0] - prev_p);
    }
    prev_w = ow[0];
    prev_p = op[0];
    CHECK(std::abs(op[0]) <= 1.5f);
  }
  CHECK(dp < dw * 0.5);  // pink: less high-frequency energy
}

TEST_CASE("noise tolerates hostile seed values") {
  for (double s : {-3.0, 1e12, -1e300}) {
    auto m = make_noise("white", s);
    float out[1];
    for (int i = 0; i < 64; ++i) {
      m->process(nullptr, out);
      CHECK(std::abs(out[0]) <= 1.0f);
    }
  }
}
