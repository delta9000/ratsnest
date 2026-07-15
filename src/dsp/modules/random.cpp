#include <algorithm>
#include <cmath>
#include <cstdint>
#include <numbers>
#include "dsp/modules.h"

namespace ms::dsp {
namespace {

constexpr InputInfo kIns[] = {{"rate", 1.f}, {"smooth", 1.f}};
constexpr ConfigInfo kCfg[] = {{"seed", nullptr}};
constexpr OutputInfo kOuts[] = {{"out"}, {"stepped"}};

class Random final : public Module {
 public:
  void init(double rate, const Config& cfg) override {
    rate_ = static_cast<float>(rate);
    // Sanitize before the cast: double -> uint32_t is UB out of range.
    double s = cfg_num(cfg, "seed", 1.0);
    if (!std::isfinite(s)) s = 1.0;
    const auto seed =
        static_cast<std::uint32_t>(std::clamp(std::abs(s), 0.0, 4294967295.0));
    state_ = seed * 2654435761u;
    if (state_ == 0) state_ = 0x9E3779B9u;
    phase_ = 0.0f;
    prev_ = draw();
    next_ = draw();
  }

  void process(const float* ins, float* outs) override {
    phase_ += std::max(ins[0], 0.0f) / rate_;
    if (phase_ >= 1.0f) {
      phase_ -= std::floor(phase_);
      prev_ = next_;
      next_ = draw();
    }
    const float sm = std::clamp(ins[1], 0.0f, 1.0f);
    const float eased =
        prev_ + (next_ - prev_) *
                    0.5f * (1.0f - std::cos(std::numbers::pi_v<float> * phase_));
    outs[0] = next_ + (eased - next_) * sm;
    outs[1] = next_;
  }

  void copy_state_from(const Module& other) override {
    const auto& o = static_cast<const Random&>(other);
    state_ = o.state_;
    phase_ = o.phase_;
    prev_ = o.prev_;
    next_ = o.next_;
  }

 private:
  // Marsaglia xorshift32; see docs/attribution.md.
  float draw() {
    std::uint32_t x = state_;
    x ^= x << 13;
    x ^= x >> 17;
    x ^= x << 5;
    state_ = x;
    return 2.0f * (static_cast<float>(x) * (1.0f / 4294967296.0f)) - 1.0f;
  }

  float rate_ = 48000.0f;
  std::uint32_t state_ = 0x9E3779B9u;
  float phase_ = 0.0f;
  float prev_ = 0.0f;
  float next_ = 0.0f;
};

std::unique_ptr<Module> make() { return std::make_unique<Random>(); }
}  // namespace

const ModuleInfo random_info{"random", kIns, kCfg, kOuts, &make};
}  // namespace ms::dsp
