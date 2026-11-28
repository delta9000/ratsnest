#include <algorithm>
#include <cmath>
#include <cstdint>
#include <numbers>
#include "dsp/modules.h"
#include "dsp/util.h"

namespace ms::dsp {
namespace {

constexpr const char* kColors[] = {"white", "pink", "brown", "blue", "violet", nullptr};
constexpr ConfigInfo kCfg[] = {{"color", kColors}, {"seed", nullptr}};
constexpr OutputInfo kOuts[] = {{"out"}};

class Noise final : public Module {
 public:
  void init(double rate, const Config& cfg) override {
    color_ = cfg_enum(cfg, "color", kColors);
    // Leaky integration: -6 dB/oct above 10 Hz, finite power at DC.
    // Gain fixes the stationary variance as rate changes. Locally derived;
    // spectral-color definitions and existing pink provenance: docs/attribution.md.
    brown_pole_ = std::exp(-2.0 * std::numbers::pi * 10.0 / rate);
    brown_gain_ = 0.5 * std::sqrt(1.0 - brown_pole_ * brown_pole_);
    brown_ = 0.0;
    previous_ = 0.0f;
    // Every color but brown (whose gain above already holds its level) keeps
    // its power per Hz at every rate: white scaled by white_gain(), the pink
    // filter's poles and weights moved so each section has the same corner
    // in Hz, and the differentiators' slope per Hz restored (all exactly 1 at
    // 48 kHz). util.h has why.
    const double k = 48000.0 / rate;
    white_ = white_gain(rate);
    slope_ = static_cast<float>(1.0 / k);
    for (int i = 0; i < 3; ++i) {
      pole_[i] = static_cast<float>(std::pow(double(kPole[i]), k));
      weight_[i] = static_cast<float>(double(kWeight[i]) * k);
    }
    // Sanitize before the cast: double -> uint32_t is UB out of range.
    state_ = cfg_seed(cfg);
    b0_ = b1_ = b2_ = 0.0f;
  }

  void process(const float*, float* outs) override {
    const float u = 2.0f * unit_float(xorshift32(state_)) - 1.0f;
    if (color_ == 2) {
      brown_ = brown_pole_ * brown_ + brown_gain_ * u;
      outs[0] = static_cast<float>(brown_);
      return;
    }
    const float w = u * white_;
    if (color_ == 0) {
      outs[0] = w;
      return;
    }
    if (color_ == 4) {
      outs[0] = 0.5f * slope_ * (w - previous_);
      previous_ = w;
      return;
    }
    // Paul Kellet's economy pink filter (-3 dB/oct approximation).
    b0_ = pole_[0] * b0_ + w * weight_[0];
    b1_ = pole_[1] * b1_ + w * weight_[1];
    b2_ = pole_[2] * b2_ + w * weight_[2];
    const float pink = (b0_ + b1_ + b2_ + w * 0.1848f) * 0.11f;
    // Differentiation raises the power-spectrum slope by 6 dB/oct below
    // Nyquist: pink -> blue, white -> violet. No nonlinear amplitude clipping.
    outs[0] = color_ == 3 ? 2.0f * slope_ * (pink - previous_) : pink;
    previous_ = pink;
  }

  void copy_state_from(const Module& other) override {
    const auto& o = static_cast<const Noise&>(other);
    state_ = o.state_;
    b0_ = o.b0_;
    b1_ = o.b1_;
    b2_ = o.b2_;
    brown_ = o.brown_;
    previous_ = color_ == o.color_ ? o.previous_ : 0.0f;
  }

 private:
  int color_ = 0;
  std::uint32_t state_ = 0x9E3779B9u;
  float b0_ = 0.0f, b1_ = 0.0f, b2_ = 0.0f;
  double brown_ = 0.0, brown_pole_ = 0.0, brown_gain_ = 0.0;
  float previous_ = 0.0f;
  static constexpr float kPole[3] = {0.99765f, 0.96300f, 0.57000f};
  static constexpr float kWeight[3] = {0.0990460f, 0.2965164f, 1.0526913f};
  float pole_[3] = {}, weight_[3] = {};
  float white_ = 1.0f, slope_ = 1.0f;
};

std::unique_ptr<Module> make() { return std::make_unique<Noise>(); }
}  // namespace

const ModuleInfo noise_info{"noise", {}, kCfg, kOuts, &make};
}  // namespace ms::dsp
