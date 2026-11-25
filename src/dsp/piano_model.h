#pragma once
// Physical piano string model behind the `piano` voice: per-key design tables
// (inharmonicity, losses, hammer felt, unison detuning) built at init, and the
// RT-safe pieces they configure: a two-segment velocity-wave digital waveguide
// per string and a nonlinear felt hammer solved implicitly each sample.
//
// Paper-derived / concept-inspired; no code copied. See docs/attribution.md
// ("piano"): Fletcher 1964 (stiff-string partials), Van Duyne & Smith 1994
// (dispersion as identical first-order allpasses), Hall / Chaigne & Askenfelt
// (power-law felt hammer), Weinreich 1977 (coupled unisons, double decay),
// Bensa et al. 2003 (frequency-dependent damping b1 + b3 f^2).
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <numbers>
#include "dsp/resonate.h"
#include "dsp/util.h"

namespace ms::dsp::piano {

constexpr int kKeys = 88;        // A0 (MIDI 21) .. C8 (MIDI 108)
constexpr int kMaxStrings = 3;   // unison strings per key
constexpr int kMaxStages = 24;   // dispersion allpass stages (deep bass)
constexpr int kFirstUndamped = 68;  // F6 and up have no damper
constexpr double kTwoPi = 2.0 * std::numbers::pi;

// Piecewise-linear curve over key index (0 = A0). Points ascend in key.
struct Pt { float key, val; };
template <int N>
float curve(const Pt (&p)[N], float key) {
  if (key <= p[0].key) return p[0].val;
  for (int i = 1; i < N; ++i)
    if (key <= p[i].key) {
      const float t = (key - p[i - 1].key) / (p[i].key - p[i - 1].key);
      return p[i - 1].val + t * (p[i].val - p[i - 1].val);
    }
  return p[N - 1].val;
}

// Phase delay (samples) of the first-order allpass (a + z^-1)/(1 + a z^-1).
inline double allpass_delay(double a, double w) {
  return 1.0 - 2.0 / w * std::atan2(a * std::sin(w), 1.0 + a * std::cos(w));
}
// Phase delay (samples) of the one-pole lowpass g(1 + a)/(1 + a z^-1).
inline double lowpass_delay(double a, double w) {
  return -std::atan2(a * std::sin(w), 1.0 + a * std::cos(w)) / w;
}
inline double lowpass_mag2(double a, double w) {
  return 1.0 + 2.0 * a * std::cos(w) + a * a;
}

// Railsback-style stretch: octaves widen toward both ends, as tuners set
// them to agree with the strings' own sharp partials. Cents at full stretch.
inline float stretch_cents(int key) {
  const auto x = static_cast<float>(key - 48);
  const float s = key < 48 ? x / 48.0f : x / 39.0f;
  return (s < 0.0f ? 20.0f : 30.0f) * s * s * s;
}

// Everything the voice needs about one key, computed once at init.
struct KeyDesign {
  double f0 = 220.0;    // ideal flexible-string fundamental at design pitch
  double B = 1e-4;      // inharmonicity coefficient
  double ap = 0.0;      // dispersion allpass coefficient
  int stages = 6;       // ... and how many identical stages
  double loss_a = 0.0;  // one-pole loss pole
  float loss_g = 0.999f;
  float gamma = 0.0f;   // symmetric-mode bridge loss (unison coupling)
  float damp = 1.0f;    // per-period gain while the damper is down
  float beta = 0.12f;   // strike point, fraction of the speaking length
  float z = 2.0f;       // string wave impedance, kg/s
  float mass = 0.008f;  // hammer mass, kg
  float k = 1e9f;       // felt stiffness, N/m^p
  float p = 2.5f;       // felt exponent
  float gain = 1.0f;    // voicing: output scale
  int strings = 3;
  float detune[kMaxStrings] = {};  // cents
};

// Hand-voiced curves. Values sit in the range published for concert grands
// (B from ~5e-5 in the low tenor to ~2e-2 at C8; contact times of a few ms
// in the bass to under 1 ms in the treble); they are not fitted to one
// instrument. Keys: 0 = A0, 20 = F2, 39 = C4, 63 = C6, 87 = C8.
inline constexpr Pt kLogB[] = {{0, -3.6f}, {20, -4.3f}, {39, -3.45f}, {63, -2.6f}, {87, -1.75f}};
inline constexpr Pt kT60[] = {{0, 30.f}, {39, 18.f}, {63, 9.f}, {87, 3.f}};  // s, fundamental
// How much faster the in-phase unison mode decays (the prompt sound).
inline constexpr Pt kPrompt[] = {{0, 4.f}, {51, 4.f}, {87, 2.f}};
inline constexpr Pt kDampT60[] = {{0, 0.6f}, {30, 0.25f}, {67, 0.08f}};  // s, damper down
inline constexpr Pt kLogZ[] = {{0, 2.4f}, {30, 1.1f}, {87, 0.53f}};   // ln kg/s
inline constexpr Pt kMass[] = {{0, 0.0105f}, {39, 0.008f}, {87, 0.0045f}};          // kg
inline constexpr Pt kFeltP[] = {{0, 2.3f}, {87, 3.0f}};
inline constexpr Pt kCrush[] = {{0, 1.1e-3f}, {39, 0.65e-3f}, {63, 0.28e-3f}, {75, 0.14e-3f}, {87, 0.06e-3f}};  // m at 2 m/s
inline constexpr Pt kBeta[] = {{0, 0.12f}, {60, 0.12f}, {87, 0.07f}};
// Voicing, dB: evens out early loudness (first 150 ms, above 200 Hz) at a
// given velocity, as a voicer would by needling hammers.
inline constexpr Pt kVoicingDb[] = {{0, 7.f}, {10, 6.f}, {12, 4.f}, {24, 3.f}, {36, 1.f}, {40, 0.f},
                                     {72, 1.f}, {78, 4.f}, {84, 7.f}, {87, 12.f}};

// Relative tuning error of partials 2..K for stage coefficient a, with the
// plain delay fixed so partial 1 lands exactly. The fit band stops near
// 4.5 kHz (36 partials at most): above it partials are weak and short-lived,
// and one shared coefficient cannot follow the whole curve.
inline double dispersion_error(double a, const KeyDesign& d, double fs) {
  const double w1 = kTwoPi * d.f0 * std::sqrt(1.0 + d.B) / fs;
  const double m = d.stages;
  const double rest = kTwoPi / w1 - m * allpass_delay(a, w1) - lowpass_delay(d.loss_a, w1);
  const double w_max = kTwoPi * std::min(4500.0, 0.42 * fs) / fs;
  double err = 0.0;
  for (int k = 2; k <= 36; ++k) {
    const double w = kTwoPi * k * d.f0 * std::sqrt(1.0 + d.B * k * k) / fs;
    if (w > w_max) break;
    const double loop = rest + m * allpass_delay(a, w) + lowpass_delay(d.loss_a, w);
    const double e = (w * loop - kTwoPi * k) / k;
    err += e * e;
  }
  return err;
}

// Golden-section search for the stage coefficient in [lo, 0], after a coarse
// scan; lo keeps the cascade's DC delay inside the loop's delay budget.
inline double fit_dispersion(const KeyDesign& d, double fs, double budget) {
  const double per = std::max(budget / d.stages, 1.0);
  double lo = std::max((1.0 - per) / (1.0 + per), -0.95);
  double best = 0.0, best_e = dispersion_error(0.0, d, fs);
  for (int i = 1; i <= 24; ++i) {
    const double a = lo * i / 24.0;
    const double e = dispersion_error(a, d, fs);
    if (e < best_e) { best_e = e; best = a; }
  }
  const double step = -lo / 24.0;
  double x0 = std::max(best - step, lo), x1 = std::min(best + step, 0.0);
  const double r = 0.6180339887498949;
  for (int i = 0; i < 30; ++i) {
    const double m0 = x1 - r * (x1 - x0), m1 = x0 + r * (x1 - x0);
    if (dispersion_error(m0, d, fs) < dispersion_error(m1, d, fs)) x1 = m1;
    else x0 = m0;
  }
  return 0.5 * (x0 + x1);
}

// One-pole loss filter matching per-period gains at the fundamental and at a
// high reference frequency, from the damping law sigma(f) = b1 + b3 f^2.
inline void design_loss(KeyDesign& d, double f1, double fs, double t60) {
  const double b1 = 6.907755 / t60;
  constexpr double b3 = 3.0e-7;
  const double fh = std::min(std::max(4000.0, 3.0 * f1), 0.42 * fs);
  const double g1 = std::exp(-b1 / f1);
  const double gh = std::exp(-(b1 + b3 * fh * fh) / f1);
  const double w1 = kTwoPi * f1 / fs, wh = kTwoPi * fh / fs;
  const double target = gh / g1;
  double lo = -0.95, hi = 0.0;
  for (int i = 0; i < 40 && fh > 1.2 * f1; ++i) {
    const double a = 0.5 * (lo + hi);
    const double ratio = std::sqrt(lowpass_mag2(a, w1) / lowpass_mag2(a, wh));
    (ratio > target ? hi : lo) = a;
  }
  d.loss_a = fh > 1.2 * f1 ? 0.5 * (lo + hi) : 0.0;
  const double g = g1 * std::sqrt(lowpass_mag2(d.loss_a, w1)) / (1.0 + d.loss_a);
  d.loss_g = static_cast<float>(std::min(g, 0.99999));
}

// The in-phase unison mode drives the bridge harder, so it decays faster.
inline void design_coupling(KeyDesign& d, float key, double f1, double t60) {
  const double prompt = curve(kPrompt, key);
  d.gamma = static_cast<float>(1.0 - std::exp(-(prompt - 1.0) * 6.907755 / (t60 * f1)));
}

inline void design_unison(KeyDesign& d, int key, std::uint32_t& rng) {
  d.strings = key < 10 ? 1 : key < 28 ? 2 : 3;
  const float spread = 0.4f + 0.8f * unit_float(xorshift32(rng));  // cents
  const float jitter = 0.3f * (unit_float(xorshift32(rng)) - 0.5f);
  d.detune[0] = d.strings == 1 ? 0.0f : -0.5f * spread;
  d.detune[1] = d.strings == 2 ? 0.5f * spread : jitter;
  d.detune[2] = 0.5f * spread + jitter;
}

inline void design_hammer(KeyDesign& d, float key) {
  d.mass = curve(kMass, key);
  d.p = curve(kFeltP, key);
  const float c = curve(kCrush, key);
  // Felt sized so a rigid stop at 2 m/s compresses it by `c`:
  // (1/2) m v^2 = K c^(p+1) / (p+1).
  d.k = static_cast<float>((d.p + 1.0) * d.mass * 4.0 / 2.0 / std::pow(c, d.p + 1.0));
  d.z = std::exp(curve(kLogZ, key));
  d.beta = curve(kBeta, key);
}

inline KeyDesign design_key(int key, double fs, std::uint32_t& rng) {
  KeyDesign d;
  const auto kf = static_cast<float>(key);
  const double f1 = 440.0 * std::exp2((key - 48) / 12.0);
  d.B = std::pow(10.0, curve(kLogB, kf));
  d.f0 = f1 / std::sqrt(1.0 + d.B);
  design_loss(d, f1, fs, curve(kT60, kf));
  const double dt60 = key >= kFirstUndamped ? 1e9 : curve(kDampT60, kf);
  d.damp = static_cast<float>(std::exp(-6.907755 / (dt60 * f1)));
  design_unison(d, key, rng);
  // A lone string has no out-of-phase partner: its one mode is the slow one.
  if (d.strings > 1) design_coupling(d, kf, f1, curve(kT60, kf));
  design_hammer(d, kf);
  const double n = fs / f1;
  // Single bass strings get the long cascade; unisons share the cost.
  d.stages = d.strings == 1 ? kMaxStages : d.strings == 2 ? 12 : 6;
  d.ap = fit_dispersion(d, fs, n - std::max(1.0, std::round(d.beta * n)) - 3.0);
  d.gain = std::pow(10.0f, curve(kVoicingDb, kf) / 20.0f);
  return d;
}

// One string: velocity waves on two segments either side of the hammer.
// `near` is the agraffe round trip (rigid, reflection -1); `far` the bridge
// round trip, closed by loss, dispersion, fine-tuning allpass and -1.
struct String {
  resonate::DelayLine near, far;
  int near_len = 1, far_len = 4, tap = 2;
  float ap = 0.0f, loss_a = 0.0f, loss_g = 0.999f, fine = 0.0f;
  float lp = 0.0f, fine_s = 0.0f;
  float ap_s[kMaxStages] = {};
  int stages = 6;

  void init(std::size_t max_len) {
    near.init(max_len / 4 + 8);
    far.init(max_len + 8);
  }
  void clear() {
    near.clear();
    far.clear();
    lp = fine_s = 0.0f;
    std::fill(std::begin(ap_s), std::end(ap_s), 0.0f);
  }
  // Tunes the loop so its first partial sits at f1 (Hz).
  void tune(const KeyDesign& d, double f1, double fs, int max_len) {
    const double w1 = kTwoPi * f1 / fs;
    const double rest = kTwoPi / w1 - d.stages * allpass_delay(d.ap, w1) -
                        lowpass_delay(d.loss_a, w1);
    near_len = std::clamp(static_cast<int>(std::lround(d.beta * fs / f1)), 1, max_len / 4);
    const double far_total = std::clamp(rest - near_len, 2.5, max_len - 2.0);
    far_len = static_cast<int>(far_total - 0.5);
    double frac = far_total - far_len;  // Thiran target in [0.5, 1.5)
    double want = frac;
    for (int i = 0; i < 3; ++i) {  // correct for the allpass's own dispersion
      const double eta = (1.0 - frac) / (1.0 + frac);
      frac = std::clamp(frac + want - allpass_delay(eta, w1), 0.05, 1.95);
    }
    fine = static_cast<float>((1.0 - frac) / (1.0 + frac));
    tap = std::max(1, far_len / 2);
    ap = static_cast<float>(d.ap);
    stages = d.stages;
    loss_a = static_cast<float>(d.loss_a);
    loss_g = d.loss_g;
  }
  // Wave arriving back at the hammer from the agraffe side.
  float from_agraffe() const { return -near.read(static_cast<std::size_t>(near_len - 1)); }
  // Wave reaching the end of the bridge round trip, before its termination.
  float at_bridge() const { return far.read(static_cast<std::size_t>(far_len - 1)); }
  // Bridge-incident wave halfway down the far line (the radiated signal).
  float bridge_tap() const { return far.read(static_cast<std::size_t>(tap - 1)); }
  // Bridge termination: loss, damper, dispersion, fine tuning, inversion.
  float terminate(float x, float damp) {
    lp = loss_g * (1.0f + loss_a) * x - loss_a * lp;
    float y = lp * damp;
    for (int i = 0; i < stages; ++i) {
      const float o = ap * y + ap_s[i];
      ap_s[i] = y - ap * o;
      y = o;
    }
    const float o = fine * y + fine_s;
    fine_s = y - fine * o;
    return -o;
  }
  void copy_from(const String& o) {
    near.copy_from(o.near);
    far.copy_from(o.far);
    near_len = o.near_len; far_len = o.far_len; tap = o.tap;
    ap = o.ap; stages = o.stages; loss_a = o.loss_a; loss_g = o.loss_g; fine = o.fine;
    lp = o.lp; fine_s = o.fine_s;
    std::copy(std::begin(o.ap_s), std::end(o.ap_s), std::begin(ap_s));
  }
  void push(float to_bridge, float to_agraffe) {
    far.write(to_bridge);
    near.write(to_agraffe);
  }
};

// Power-law felt hammer, F = K max(c, 0)^p. Each sample solves the implicit
// contact force against the strings' impedance by safeguarded Newton steps,
// so stiff treble felt stays stable at audio rate.
struct Hammer {
  float y = 0.0f, v = 0.0f, ys = 0.0f;
  float mass = 0.008f, k = 1e9f, p = 2.5f, z2n = 4.0f;
  int life = 0;

  void strike(const KeyDesign& d, float speed, float hardness, float fs) {
    mass = d.mass;
    k = d.k * hardness;
    p = d.p;
    z2n = 2.0f * d.z * static_cast<float>(d.strings);
    y = ys = 0.0f;
    v = speed;
    life = static_cast<int>(0.05f * fs);
  }
  // String velocity v0 (no force) at the strike point -> total force.
  float step(float v0, float dt) {
    if (life <= 0) return 0.0f;
    --life;
    const float alpha = dt * dt / mass + dt / z2n;
    const float pred = (y + v * dt) - (ys + v0 * dt);
    float f = 0.0f;
    if (pred > 0.0f) f = solve(pred, alpha);
    v -= f / mass * dt;
    y += v * dt;
    ys += (v0 + f / z2n) * dt;
    return f;
  }
  float solve(float pred, float alpha) const {
    float lo = 0.0f, hi = pred / alpha;
    float f = 0.5f * hi;
    for (int i = 0; i < 12; ++i) {
      const float c = std::max(pred - f * alpha, 0.0f);
      const float g = f - k * std::pow(c, p);
      if (std::abs(g) < 1e-4f * (1.0f + f)) break;
      (g > 0.0f ? hi : lo) = f;
      const float dg = 1.0f + k * p * alpha * std::pow(std::max(c, 1e-12f), p - 1.0f);
      f -= g / dg;
      if (!(f > lo && f < hi)) f = 0.5f * (lo + hi);
    }
    return f;
  }
};

}  // namespace ms::dsp::piano
