#include <doctest/doctest.h>
#include <cmath>
#include <memory>
#include <numbers>
#include <vector>
#include "dsp/modules.h"
#include "dsp/piano_model.h"

using namespace ms::dsp;

namespace {
constexpr double kFs = 48000.0;

// gate, voct, vel, hardness, detune, decay, stretch, pedal
struct PianoRig {
  std::unique_ptr<Module> m;
  float ins[8] = {0.f, 0.f, 0.5f, 0.5f, 1.f, 1.f, 1.f, 0.f};
  float out[1] = {};
  PianoRig() {
    const ModuleInfo* info = find_module("piano");
    REQUIRE(info != nullptr);
    m = info->create();
    m->init(kFs, {});
  }
  void note(int midi, float vel) {
    ins[1] = static_cast<float>(midi - 57) / 12.0f;
    ins[2] = vel;
    ins[0] = 0.f;
    m->process(ins, out);
    ins[0] = 1.f;
  }
  // Runs n samples, gate dropping after `hold` of them.
  std::vector<float> run(int n, int hold = 1 << 30) {
    std::vector<float> y(static_cast<std::size_t>(n));
    for (int i = 0; i < n; ++i) {
      if (i == hold) ins[0] = 0.f;
      m->process(ins, out);
      y[static_cast<std::size_t>(i)] = out[0];
    }
    return y;
  }
};

double rms(const std::vector<float>& x, double t0, double t1) {
  const auto a = static_cast<std::size_t>(t0 * kFs), b = static_cast<std::size_t>(t1 * kFs);
  double s = 0.0;
  for (std::size_t i = a; i < b; ++i) s += static_cast<double>(x[i]) * x[i];
  return std::sqrt(s / static_cast<double>(b - a));
}
double db(double v) { return 20.0 * std::log10(v + 1e-30); }

// Hann-windowed DFT magnitude of x[from, from + n) at frequency f.
double dft_mag(const std::vector<float>& x, std::size_t from, std::size_t n, double f) {
  double re = 0.0, im = 0.0;
  const double w = 2.0 * std::numbers::pi * f / kFs;
  for (std::size_t i = 0; i < n; ++i) {
    const double win = 0.5 - 0.5 * std::cos(2.0 * std::numbers::pi * static_cast<double>(i) / static_cast<double>(n));
    const double v = win * x[from + i];
    re += v * std::cos(w * static_cast<double>(i));
    im -= v * std::sin(w * static_cast<double>(i));
  }
  return std::hypot(re, im);
}
// Strongest frequency within +-span cents of f, in 0.05-cent steps.
double peak_freq(const std::vector<float>& x, double f, double span) {
  double best = f, best_m = -1.0;
  for (double c = -span; c <= span; c += 0.05) {
    const double fc = f * std::exp2(c / 1200.0);
    const double m = dft_mag(x, 4800, 48000, fc);
    if (m > best_m) { best_m = m; best = fc; }
  }
  return best;
}
double cents(double a, double b) { return 1200.0 * std::log2(a / b); }
}  // namespace

TEST_CASE("piano: equal-tempered fundamental lands within half a cent") {
  for (int midi : {33, 60, 81}) {
    PianoRig r;
    r.ins[4] = 0.f;  // no unison detune
    r.ins[6] = 0.f;  // no stretch
    r.note(midi, 0.5f);
    const auto y = r.run(60000);
    const double f = 440.0 * std::exp2((midi - 69) / 12.0);
    CHECK(std::abs(cents(peak_freq(y, f, 20.0), f)) < 0.5);
  }
}

TEST_CASE("piano: stretch tuning widens the octaves at the ends") {
  CHECK(piano::stretch_cents(0) == doctest::Approx(-20.0f));
  CHECK(piano::stretch_cents(87) == doctest::Approx(30.0f));
  CHECK(std::abs(piano::stretch_cents(48)) < 1e-6f);
  CHECK(piano::stretch_cents(30) < 0.0f);
  CHECK(piano::stretch_cents(70) > 0.0f);
}

TEST_CASE("piano: partials follow the stiff-string law") {
  // C3: B from the design, partial k sits at k f0 sqrt(1 + B k^2).
  const int midi = 48, key = midi - 21;
  std::uint32_t rng = 1;
  piano::KeyDesign d;
  for (int k = 0; k <= key; ++k) d = piano::design_key(k, kFs, rng);
  PianoRig r;
  r.ins[4] = 0.f;
  r.ins[6] = 0.f;
  r.note(midi, 0.7f);
  const auto y = r.run(60000);
  const double f1 = 440.0 * std::exp2((midi - 69) / 12.0);
  const double f0 = f1 / std::sqrt(1.0 + d.B);
  for (int k : {4, 8, 12}) {
    const double want = k * f0 * std::sqrt(1.0 + d.B * k * k);
    const double sharp = cents(want, k * f1);  // how inharmonic it should be
    const double got = peak_freq(y, want, 25.0);
    CAPTURE(k);
    CHECK(sharp > 1.0);
    CHECK(std::abs(cents(got, want)) < 6.0);
  }
}

TEST_CASE("piano: harder hits are louder and brighter") {
  double prev_peak = 0.0, prev_bright = 0.0;
  for (float vel : {0.1f, 0.5f, 1.0f}) {
    PianoRig r;
    r.note(60, vel);
    const auto y = r.run(9600);
    double peak = 0.0, e = 0.0, de = 0.0;
    for (std::size_t i = 1; i < y.size(); ++i) {
      peak = std::max(peak, static_cast<double>(std::abs(y[i])));
      e += static_cast<double>(y[i]) * y[i];
      const double dy = y[i] - y[i - 1];
      de += dy * dy;
    }
    const double bright = de / e;  // energy of the derivative: a centroid proxy
    CAPTURE(vel);
    CHECK(peak > prev_peak * 1.8);
    CHECK(bright > prev_bright);
    prev_peak = peak;
    prev_bright = bright;
  }
  // pp to ff spans the range of an acoustic grand (~30 dB of peak level).
}

TEST_CASE("piano: velocity spans about 30 dB from pp to ff") {
  PianoRig pp, ff;
  pp.note(60, 0.1f);
  ff.note(60, 1.0f);
  const auto a = pp.run(9600), b = ff.run(9600);
  double pa = 0.0, pb = 0.0;
  for (float v : a) pa = std::max(pa, static_cast<double>(std::abs(v)));
  for (float v : b) pb = std::max(pb, static_cast<double>(std::abs(v)));
  CHECK(db(pb / pa) > 24.0);
  CHECK(db(pb / pa) < 40.0);
  CHECK(db(pb) < -3.0);  // a single fortissimo note leaves chord headroom
}

TEST_CASE("piano: two-stage decay (prompt sound, then aftersound)") {
  PianoRig r;
  r.note(60, 0.6f);
  const auto y = r.run(static_cast<int>(9 * kFs));
  const double early = db(rms(y, 0.1, 0.3)) - db(rms(y, 1.6, 1.8));  // dB over 1.5 s
  const double late = db(rms(y, 5.0, 5.2)) - db(rms(y, 6.5, 6.7));   // dB over 1.5 s
  CHECK(early > 2.0 * late);
  CHECK(late > 0.5);  // still decaying, not a drone
}

TEST_CASE("piano: dampers stop a note; the pedal and the top octaves ring on") {
  const int hold = 4800, n = 48000;
  // dB the release loses against the same note held down.
  auto damping = [&](int midi, float pedal) {
    PianoRig held, let_go;
    held.note(midi, 0.6f);
    let_go.ins[7] = pedal;
    let_go.note(midi, 0.6f);
    const auto a = held.run(n), b = let_go.run(n, hold);
    return db(rms(a, 0.6, 0.7)) - db(rms(b, 0.6, 0.7));
  };
  CHECK(damping(60, 0.f) > 40.0);           // damped C4
  CHECK(std::abs(damping(60, 1.f)) < 0.5);  // pedal down: as if held
  CHECK(std::abs(damping(91, 0.f)) < 0.5);  // G6 has no damper
}

TEST_CASE("piano: a decayed voice goes exactly silent") {
  PianoRig r;
  r.note(72, 0.5f);
  const auto y = r.run(static_cast<int>(2 * kFs), 2400);
  for (std::size_t i = y.size() - 4800; i < y.size(); ++i) REQUIRE(y[i] == 0.0f);
}

TEST_CASE("piano: stealing a ringing voice for another key does not click") {
  PianoRig r;
  r.note(48, 1.0f);
  auto y = r.run(24000);
  const float last = y.back();
  r.note(84, 0.3f);
  const auto z = r.run(480);
  CHECK(std::abs(z[0] - last) < 0.02f);
  for (std::size_t i = 1; i < z.size(); ++i) CHECK(std::abs(z[i] - z[i - 1]) < 0.05f);
}

TEST_CASE("piano: re-striking a ringing string stays bounded") {
  PianoRig r;
  for (int i = 0; i < 20; ++i) {
    r.note(36, 1.0f);
    const auto y = r.run(1200);
    for (float v : y) REQUIRE(std::abs(v) < 2.0f);
  }
}

TEST_CASE("piano: state migrates across hot reload") {
  PianoRig a, b;
  a.note(55, 0.7f);
  a.run(9000);
  b.m->copy_state_from(*a.m);
  for (int i = 0; i < 2000; ++i) {
    a.m->process(a.ins, a.out);
    b.m->process(a.ins, b.out);
    REQUIRE(a.out[0] == b.out[0]);
  }
}

TEST_CASE("soundboard: silence in, silence out") {
  const ModuleInfo* info = find_module("soundboard");
  REQUIRE(info != nullptr);
  auto m = info->create();
  m->init(kFs, {});
  float ins[4] = {0.f, 1.f, 0.4f, 0.5f}, outs[2];
  for (int i = 0; i < 4800; ++i) {
    m->process(ins, outs);
    REQUIRE(outs[0] == 0.0f);
    REQUIRE(outs[1] == 0.0f);
  }
}

TEST_CASE("soundboard: the pedal frees the sympathetic strings") {
  auto tail = [](float pedal) {
    const ModuleInfo* info = find_module("soundboard");
    auto m = info->create();
    m->init(kFs, {});
    PianoRig p;
    p.note(60, 0.8f);
    float ins[4] = {0.f, pedal, 0.4f, 0.5f}, outs[2];
    double e = 0.0;
    for (int i = 0; i < 48000; ++i) {
      if (i == 4800) p.ins[0] = 0.f;  // staccato: the C4's own damper drops
      p.m->process(p.ins, p.out);
      ins[0] = p.out[0];
      m->process(ins, outs);
      if (i > 24000) e += static_cast<double>(outs[0]) * outs[0] + outs[1] * outs[1];
    }
    return e;
  };
  CHECK(db(std::sqrt(tail(1.f) / tail(0.f))) > 20.0);
}
