#include <algorithm>
#include <array>
#include <cmath>
#include <vector>
#include "dsp/biquad.h"
#include "dsp/modules.h"
#include "dsp/piano_model.h"
#include "dsp/util.h"

// Shared piano body: radiation filter, a short feedback-delay-network
// soundboard, and 88 sympathetic strings whose dampers follow `pedal`.
// Concept-inspired by Bank et al. 2003 (FDN soundboard); see
// docs/attribution.md ("piano").

namespace ms::dsp {
namespace {

constexpr InputInfo kIns[] = {{"in", 0.f}, {"pedal", 0.f}, {"body", 0.4f}, {"resonance", 0.5f}};
constexpr OutputInfo kOuts[] = {{"l"}, {"r"}};
constexpr int kLines = 8;
constexpr int kBlock = 32;
// Soundboard path lengths (ms): mutually prime at 48 kHz.
constexpr float kLineMs[kLines] = {2.9f, 4.3f, 5.9f, 7.7f, 9.7f, 12.1f, 14.9f, 18.3f};
constexpr float kOutSign[kLines] = {1, -1, 1, 1, -1, 1, -1, -1};

// One undamped-or-damped string: a Karplus-Strong comb at a key's pitch.
struct Sym {
  int base = 0, len = 0, pos = 0;
  float delay = 0.0f, g = 0.0f, f = 0.0f, pan_l = 0.5f, pan_r = 0.5f, last = 0.0f;
};

class Soundboard final : public Module {
 public:
  void init(double rate, const Config&) override {
    fs_ = static_cast<float>(rate);
    hp_ = make_hpf(70.0f, fs_);
    hp_s_ = {};
    int total = 0;
    for (int k = 0; k < piano::kKeys; ++k) {
      Sym& s = sym_[k];
      s.f = 440.0f * std::exp2((static_cast<float>(k - 48) + piano::stretch_cents(k) / 100.0f) / 12.0f);
      s.delay = fs_ / s.f - 0.5f;  // the two-tap average adds half a sample
      s.len = static_cast<int>(s.delay) + 3;
      s.base = total;
      total += s.len;
      const float pan = -0.6f + 1.2f * static_cast<float>(k) / (piano::kKeys - 1);
      s.pan_l = std::sqrt(0.5f * (1.0f - pan));
      s.pan_r = std::sqrt(0.5f * (1.0f + pan));
    }
    sym_buf_.assign(static_cast<std::size_t>(total), 0.0f);
    int fdn_total = 0;
    for (int i = 0; i < kLines; ++i) {
      line_len_[i] = std::max(2, static_cast<int>(kLineMs[i] * 0.001f * fs_));
      line_base_[i] = fdn_total;
      fdn_total += line_len_[i];
      // Body T60 ~0.3 s at low frequencies; the per-line lowpass shortens it.
      line_g_[i] = std::pow(10.0f, -3.0f * static_cast<float>(line_len_[i]) / (0.3f * fs_));
    }
    fdn_buf_.assign(static_cast<std::size_t>(fdn_total), 0.0f);
    fdn_lp_.fill(0.0f);
    fdn_pos_.fill(0);
    lp_c_ = std::exp(-2.0f * std::numbers::pi_v<float> * 2500.0f / fs_);
    pedal_ = -1.0f;
    countdown_ = 0;
  }

  void process(const float* ins, float* outs) override {
    if (countdown_-- <= 0) update(ins);
    const float x = sanitize(ins[0]);
    const float dry = Biquad::process(x, hp_, hp_s_);
    float bl = 0.0f, br = 0.0f;
    body(dry, bl, br);
    float sl = 0.0f, sr = 0.0f;
    sympathetic(dry * coupling_, sl, sr);
    outs[0] = dry + body_ * bl + sl;
    outs[1] = dry + body_ * br + sr;
  }

  void copy_state_from(const Module& other) override {
    const auto& o = static_cast<const Soundboard&>(other);
    if (o.sym_buf_.size() != sym_buf_.size() || o.fdn_buf_.size() != fdn_buf_.size()) return;
    sym_buf_ = o.sym_buf_;  // same sizes: assignment reuses storage
    fdn_buf_ = o.fdn_buf_;
    sym_ = o.sym_;
    fdn_lp_ = o.fdn_lp_;
    fdn_pos_ = o.fdn_pos_;
    hp_s_ = o.hp_s_;
  }

 private:
  void update(const float* ins) {
    countdown_ = kBlock - 1;
    body_ = clamp_nan(ins[2], 0.0f, 1.0f);
    coupling_ = 4e-3f * clamp_nan(ins[3], 0.0f, 2.0f);
    const float pedal = clamp_nan(ins[1], 0.0f, 1.0f);
    if (std::abs(pedal - pedal_) < 1e-3f) return;
    // Lifting the dampers is not instant: a ~17 ms glide per step.
    pedal_ = pedal_ < 0.0f ? pedal : pedal_ + (pedal - pedal_) * 0.04f;
    for (int k = 0; k < piano::kKeys; ++k) {
      const auto kf = static_cast<float>(k);
      const float free_t = 0.5f * piano::curve(piano::kT60, kf);
      const float damped = k >= piano::kFirstUndamped ? free_t : 0.6f * piano::curve(piano::kDampT60, kf);
      const float t60 = damped * std::pow(free_t / damped, pedal_);
      sym_[k].g = std::exp(-6.907755f / (t60 * sym_[k].f));
    }
  }

  // Eight-line FDN, Hadamard feedback, per-line lowpass damping.
  void body(float x, float& l, float& r) {
    std::array<float, kLines> v{};
    for (int i = 0; i < kLines; ++i) {
      const int rd = line_base_[i] + fdn_pos_[i];
      fdn_lp_[i] = fdn_buf_[static_cast<std::size_t>(rd)] * (1.0f - lp_c_) + lp_c_ * fdn_lp_[i];
      v[i] = fdn_lp_[i] * line_g_[i];
      l += kOutSign[i] * v[i] * (i % 2 ? 0.6f : 1.0f);
      r += kOutSign[i] * v[i] * (i % 2 ? 1.0f : 0.6f);
    }
    for (int h = 1; h < kLines; h <<= 1)
      for (int i = 0; i < kLines; i += 2 * h)
        for (int j = i; j < i + h; ++j) {
          const float a = v[j], b = v[j + h];
          v[j] = a + b;
          v[j + h] = a - b;
        }
    for (int i = 0; i < kLines; ++i) {
      float w = v[i] * 0.35355339f + x * kOutSign[i] * 0.5f;
      if (std::abs(w) < 1e-20f) w = 0.0f;
      fdn_buf_[static_cast<std::size_t>(line_base_[i] + fdn_pos_[i])] = w;
      fdn_pos_[i] = fdn_pos_[i] + 1 == line_len_[i] ? 0 : fdn_pos_[i] + 1;
    }
    l *= 0.25f;
    r *= 0.25f;
  }

  void sympathetic(float x, float& l, float& r) {
    for (auto& s : sym_) {
      float* buf = &sym_buf_[static_cast<std::size_t>(s.base)];
      // buf[pos + k] was written k + 1 samples ago.
      const float rp = static_cast<float>(s.pos) + s.delay - 1.0f;
      const int i0 = static_cast<int>(rp);
      const float fr = rp - static_cast<float>(i0);
      const float a = buf[i0 % s.len], b = buf[(i0 + 1) % s.len];
      const float d = a + fr * (b - a);
      float y = x + s.g * 0.5f * (d + s.last);
      if (std::abs(y) < 1e-20f) y = 0.0f;
      s.last = d;
      s.pos = s.pos == 0 ? s.len - 1 : s.pos - 1;
      buf[s.pos] = y;
      l += y * s.pan_l;
      r += y * s.pan_r;
    }
  }

  std::array<Sym, piano::kKeys> sym_{};
  std::vector<float> sym_buf_, fdn_buf_;
  std::array<float, kLines> fdn_lp_{}, line_g_{};
  std::array<int, kLines> fdn_pos_{}, line_len_{}, line_base_{};
  BiquadCoeffs hp_;
  BiquadState hp_s_;
  float fs_ = 48000.0f, lp_c_ = 0.7f, body_ = 0.4f, coupling_ = 1e-3f, pedal_ = -1.0f;
  int countdown_ = 0;
};

std::unique_ptr<Module> make() { return std::make_unique<Soundboard>(); }
}  // namespace
const ModuleInfo soundboard_info{"soundboard", kIns, {}, kOuts, &make};
}  // namespace ms::dsp
