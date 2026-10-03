#include <algorithm>
#include <cmath>
#include <cstdint>
#include <numbers>
#include "dsp/modules.h"

namespace ms::dsp {
namespace {

constexpr const char* kColors[] = {"white", "pink", "brown", "blue", "violet", nullptr};
constexpr ConfigInfo kCfg[] = {{"color", kColors}, {"seed", nullptr}};
constexpr OutputInfo kOuts[] = {{"out"}};

class Noise final : public Module {
 public:
  void init(double rate, const Config& cfg) override {
    const auto color = cfg_str(cfg, "color", "white");
    for (int i = 0; kColors[i]; ++i) if (color == kColors[i]) color_ = i;
    // Leaky integration: -6 dB/oct above 10 Hz, finite power at DC.
    // Gain fixes the stationary variance as rate changes. Locally derived;
    // spectral-color definitions and existing pink provenance: docs/attribution.md.
    brown_pole_ = std::exp(-2.0 * std::numbers::pi * 10.0 / rate);
    brown_gain_ = 0.5 * std::sqrt(1.0 - brown_pole_ * brown_pole_);
    brown_ = 0.0;
    previous_ = 0.0f;
    // Sanitize before the cast: double -> uint32_t is UB out of range.
    double s = cfg_num(cfg, "seed", 1.0);
    if (!std::isfinite(s)) s = 1.0;
    const auto seed =
        static_cast<std::uint32_t>(std::clamp(std::abs(s), 0.0, 4294967295.0));
    state_ = seed * 2654435761u;
    if (state_ == 0) state_ = 0x9E3779B9u;
    b0_ = b1_ = b2_ = 0.0f;
  }

  void process(const float*, float* outs) override {
    // Marsaglia xorshift32; see docs/attribution.md.
    std::uint32_t x = state_;
    x ^= x << 13;
    x ^= x >> 17;
    x ^= x << 5;
    state_ = x;
    const float w = 2.0f * (static_cast<float>(x) * (1.0f / 4294967296.0f)) - 1.0f;
    if (color_ == 0) {
      outs[0] = w;
      return;
    }
    if (color_ == 2) {
      brown_ = brown_pole_ * brown_ + brown_gain_ * w;
      outs[0] = static_cast<float>(brown_);
      return;
    }
    if (color_ == 4) {
      outs[0] = 0.5f * (w - previous_);
      previous_ = w;
      return;
    }
    // Paul Kellet's economy pink filter (-3 dB/oct approximation).
    b0_ = 0.99765f * b0_ + w * 0.0990460f;
    b1_ = 0.96300f * b1_ + w * 0.2965164f;
    b2_ = 0.57000f * b2_ + w * 1.0526913f;
    const float pink = (b0_ + b1_ + b2_ + w * 0.1848f) * 0.11f;
    // Differentiation raises the power-spectrum slope by 6 dB/oct below
    // Nyquist: pink -> blue, white -> violet. No nonlinear amplitude clipping.
    outs[0] = color_ == 3 ? 2.0f * (pink - previous_) : pink;
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
};

std::unique_ptr<Module> make() { return std::make_unique<Noise>(); }
}  // namespace

const ModuleInfo noise_info{"noise", {}, kCfg, kOuts, &make};
}  // namespace ms::dsp
