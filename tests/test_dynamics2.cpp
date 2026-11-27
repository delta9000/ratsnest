// gate, transient and crossover: the dynamics and band-split modules.
#include <doctest/doctest.h>
#include <cmath>
#include <numbers>
#include <vector>
#include "dsp/modules.h"

using namespace ms::dsp;

namespace {
constexpr double kRate = 48000.0;

std::unique_ptr<Module> make(const char* type, Config cfg = {}) {
  const ModuleInfo* info = find_module(type);
  REQUIRE(info != nullptr);
  auto m = info->create();
  m->init(kRate, cfg);
  return m;
}

std::vector<float> defaults(const char* type) {
  std::vector<float> v;
  for (const auto& in : find_module(type)->inputs) v.push_back(in.def);
  return v;
}

float tone(int i, float amp, float hz = 220.0f) {
  return amp * std::sin(2.0f * std::numbers::pi_v<float> * hz * static_cast<float>(i) /
                        static_cast<float>(kRate));
}

double rms_db(const std::vector<float>& v, size_t from, size_t to) {
  double s = 0.0;
  for (size_t i = from; i < to; ++i) s += static_cast<double>(v[i]) * v[i];
  return 10.0 * std::log10(std::max(s / static_cast<double>(to - from), 1e-30));
}
}  // namespace

// ---- gate -------------------------------------------------------------------

TEST_CASE("gate: passes above the threshold, cuts by `range` below") {
  auto g = make("gate");
  auto ins = defaults("gate");
  float out[5];
  std::vector<float> o;
  for (int i = 0; i < 96000; ++i) {
    const float a = i < 48000 ? 0.5f : 0.001f;  // -6 dBFS, then -60 dBFS
    ins[0] = ins[1] = tone(i, a);
    g->process(ins.data(), out);
    o.push_back(out[0]);
    if (i == 40000) CHECK(out[4] == 1.0f);
  }
  CHECK(out[4] == 0.0f);
  CHECK(rms_db(o, 24000, 48000) == doctest::Approx(20.0 * std::log10(0.5 / std::sqrt(2.0))).epsilon(0.01));
  // -60 dBFS is 20 dB under -40: 10:1 wants 180 dB, `range` caps it at 60.
  const double quiet_in = 20.0 * std::log10(0.001 / std::sqrt(2.0));
  CHECK(rms_db(o, 72000, 96000) == doctest::Approx(quiet_in - 60.0).epsilon(0.01));
}

TEST_CASE("gate: ratio 2 is a gentle expander; hold bridges a short gap") {
  auto g = make("gate");
  auto ins = defaults("gate");
  ins[5] = 2.0f;    // ratio
  ins[4] = -30.0f;  // threshold
  float out[5];
  for (int i = 0; i < 48000; ++i) {
    ins[0] = ins[1] = tone(i, std::pow(10.0f, -40.0f / 20.0f));  // 10 dB under
    g->process(ins.data(), out);
  }
  CHECK(out[3] == doctest::Approx(10.0f).epsilon(0.02));  // 10 dB under -> 10 dB cut

  auto h = make("gate");
  auto hi = defaults("gate");
  hi[8] = 0.05f;  // 50 ms hold
  for (int i = 0; i < 24000; ++i) {
    hi[0] = hi[1] = tone(i, 0.5f);
    h->process(hi.data(), out);
  }
  for (int i = 0; i < 960; ++i) {  // a 20 ms gap of silence
    hi[0] = hi[1] = 0.0f;
    h->process(hi.data(), out);
  }
  CHECK(out[3] < 0.01f);  // still open
  CHECK(out[4] == 1.0f);
}

TEST_CASE("gate: external key opens it, hot reload keeps the state") {
  auto g = make("gate", {{"sidechain", ConfigValue{"external", 0.0, {}, false}}});
  auto ins = defaults("gate");
  float out[5];
  for (int i = 0; i < 9600; ++i) {
    ins[0] = ins[1] = tone(i, 0.5f);   // loud programme, silent key: closed
    g->process(ins.data(), out);
  }
  CHECK(out[3] > 50.0f);
  for (int i = 0; i < 9600; ++i) {
    ins[2] = ins[3] = 0.5f;            // key opens it
    g->process(ins.data(), out);
  }
  CHECK(out[3] < 0.01f);
  auto fresh = make("gate", {{"sidechain", ConfigValue{"external", 0.0, {}, false}}});
  fresh->copy_state_from(*g);
  float o2[5];
  g->process(ins.data(), out);
  fresh->process(ins.data(), o2);
  CHECK(o2[0] == out[0]);
}

// ---- transient --------------------------------------------------------------

namespace {
// A struck tone: instant onset, 150 ms exponential tail, every 0.5 s.
float hit(int i) {
  const int t = i % 24000;
  return tone(i, std::exp(-static_cast<float>(t) / 7200.0f), 330.0f);
}
// Level (dB) of out over the first 5 ms and over 200..300 ms of each hit.
std::pair<double, double> shape(float attack, float sustain) {
  auto m = make("transient");
  auto ins = defaults("transient");
  ins[2] = attack;
  ins[3] = sustain;
  float out[3];
  std::vector<float> o;
  for (int i = 0; i < 48000; ++i) {
    ins[0] = ins[1] = hit(i);
    m->process(ins.data(), out);
    o.push_back(out[0]);
  }
  return {rms_db(o, 24000, 24240), rms_db(o, 33600, 38400)};
}
}  // namespace

TEST_CASE("transient: zero knobs are an exact pass") {
  auto m = make("transient");
  auto ins = defaults("transient");
  float out[3];
  for (int i = 0; i < 48000; ++i) {
    ins[0] = hit(i);
    ins[1] = -hit(i);
    m->process(ins.data(), out);
    REQUIRE(out[0] == ins[0]);
    REQUIRE(out[1] == ins[1]);
  }
}

TEST_CASE("transient: attack lifts the onset, sustain lifts the tail") {
  const auto flat = shape(0.0f, 0.0f);
  const auto punch = shape(9.0f, 0.0f);
  const auto soft = shape(-9.0f, 0.0f);
  const auto ring = shape(0.0f, 9.0f);
  const auto dry = shape(0.0f, -9.0f);
  CHECK(punch.first > flat.first + 3.0);
  CHECK(std::abs(punch.second - flat.second) < 1.5);  // tail barely moves
  CHECK(soft.first < flat.first - 3.0);
  CHECK(ring.second > flat.second + 3.0);
  CHECK(std::abs(ring.first - flat.first) < 1.5);     // onset barely moves
  CHECK(dry.second < flat.second - 3.0);
}

TEST_CASE("transient: a steady tone is left alone") {
  auto m = make("transient");
  auto ins = defaults("transient");
  ins[2] = 12.0f;
  ins[3] = 12.0f;
  float out[3];
  for (int i = 0; i < 48000; ++i) {
    ins[0] = ins[1] = tone(i, 0.5f);
    m->process(ins.data(), out);
  }
  CHECK(out[2] == doctest::Approx(1.0f).epsilon(0.05));
}

// ---- crossover --------------------------------------------------------------

namespace {
// Gain (dB) of each band and of their sum at `hz`, steady state.
struct Bands { double low, mid, high, sum; };
Bands bands_at(float hz) {
  auto m = make("crossover");
  auto ins = defaults("crossover");
  float out[6];
  double sl = 0, sm = 0, sh = 0, ss = 0, si = 0;
  for (int i = 0; i < 48000; ++i) {
    ins[0] = ins[1] = tone(i, 0.5f, hz);
    m->process(ins.data(), out);
    if (i < 24000) continue;
    sl += out[0] * out[0];
    sm += out[2] * out[2];
    sh += out[4] * out[4];
    const double s = out[0] + out[2] + out[4];
    ss += s * s;
    si += ins[0] * ins[0];
  }
  auto db = [&](double e) { return 10.0 * std::log10(std::max(e / si, 1e-30)); };
  return {db(sl), db(sm), db(sh), db(ss)};
}
}  // namespace

TEST_CASE("crossover: bands sum flat and split where asked") {
  for (float hz : {40.0f, 200.0f, 700.0f, 2000.0f, 9000.0f})
    CHECK(std::abs(bands_at(hz).sum) < 0.05);
  const Bands at_lo = bands_at(200.0f), at_hi = bands_at(2000.0f);
  CHECK(at_lo.low == doctest::Approx(-6.0).epsilon(0.03));   // LR4: -6 dB at the split
  CHECK(at_hi.high == doctest::Approx(-6.0).epsilon(0.03));
  const Bands bass = bands_at(40.0f), air = bands_at(9000.0f), mid = bands_at(700.0f);
  CHECK(bass.low > -0.1);
  CHECK(bass.high < -60.0);
  CHECK(air.high > -0.1);
  CHECK(air.low < -60.0);
  CHECK(mid.mid > -1.0);
}

TEST_CASE("crossover: high below low is held at low, and still sums flat") {
  auto m = make("crossover");
  auto ins = defaults("crossover");
  ins[2] = 1000.0f;
  ins[3] = 300.0f;
  float out[6];
  double sum = 0.0, in = 0.0;
  for (int i = 0; i < 24000; ++i) {
    ins[0] = ins[1] = tone(i, 0.5f, 600.0f);
    m->process(ins.data(), out);
    if (i < 12000) continue;
    const double s = out[0] + out[2] + out[4];
    sum += s * s;
    in += ins[0] * ins[0];
  }
  CHECK(std::abs(10.0 * std::log10(sum / in)) < 0.05);
}
