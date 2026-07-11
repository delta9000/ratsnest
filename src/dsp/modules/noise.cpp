#include <algorithm>
#include <cstdint>
#include "dsp/modules.h"

namespace ms::dsp {
namespace {

constexpr const char* kColors[] = {"white", "pink", nullptr};
constexpr ConfigInfo kCfg[] = {{"color", kColors}, {"seed", nullptr}};
constexpr OutputInfo kOuts[] = {{"out"}};

class Noise final : public Module {
 public:
  void init(double, const Config& cfg) override {
    pink_ = cfg_str(cfg, "color", "white") == "pink";
    const auto seed = static_cast<std::uint32_t>(cfg_num(cfg, "seed", 1.0));
    state_ = seed * 2654435761u;
    if (state_ == 0) state_ = 0x9E3779B9u;
    b0_ = b1_ = b2_ = 0.0f;
  }

  void process(const float*, float* outs) override {
    std::uint32_t x = state_;
    x ^= x << 13;
    x ^= x >> 17;
    x ^= x << 5;
    state_ = x;
    const float w = 2.0f * (static_cast<float>(x) * (1.0f / 4294967296.0f)) - 1.0f;
    if (!pink_) {
      outs[0] = w;
      return;
    }
    // Paul Kellet's economy pink filter (-3 dB/oct approximation).
    b0_ = 0.99765f * b0_ + w * 0.0990460f;
    b1_ = 0.96300f * b1_ + w * 0.2965164f;
    b2_ = 0.57000f * b2_ + w * 1.0526913f;
    outs[0] = std::clamp((b0_ + b1_ + b2_ + w * 0.1848f) * 0.11f, -1.5f, 1.5f);
  }

  void copy_state_from(const Module& other) override {
    const auto& o = static_cast<const Noise&>(other);
    state_ = o.state_;
    b0_ = o.b0_;
    b1_ = o.b1_;
    b2_ = o.b2_;
  }

 private:
  bool pink_ = false;
  std::uint32_t state_ = 0x9E3779B9u;
  float b0_ = 0.0f, b1_ = 0.0f, b2_ = 0.0f;
};

std::unique_ptr<Module> make() { return std::make_unique<Noise>(); }
}  // namespace

const ModuleInfo noise_info{"noise", {}, kCfg, kOuts, &make};
}  // namespace ms::dsp
