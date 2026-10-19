#include <algorithm>
#include <cmath>
#include <cstdint>
#include "dsp/modules.h"

namespace ms::dsp {
namespace {
constexpr InputInfo kIns[] = {{"rate", 10.f}};
constexpr ConfigInfo kCfg[] = {{"seed", nullptr}};
constexpr OutputInfo kOuts[] = {{"out"}, {"trig"}};

class Dust final : public Module {
 public:
  void init(double rate, const Config& cfg) override {
    rate_ = rate;
    double seed = cfg_num(cfg, "seed", 1.0);
    if (!std::isfinite(seed)) seed = 1;
    state_ = static_cast<uint32_t>(std::clamp(std::abs(seed), 0.0, 4294967295.0)) * 2654435761u;
    if (!state_) state_ = 0x9E3779B9u;
  }
  void process(const float* ins, float* outs) override {
    // Marsaglia xorshift32; see docs/attribution.md. One Bernoulli trial per
    // sample, with a uniform bipolar amplitude conditional on an event.
    state_ ^= state_ << 13; state_ ^= state_ >> 17; state_ ^= state_ << 5;
    const double u = static_cast<double>(state_) / 4294967296.0;
    const double p = std::isfinite(ins[0]) ? std::clamp(ins[0] / rate_, 0.0, 1.0) : 0.0;
    const bool event = u < p;
    outs[0] = event ? static_cast<float>(2.0 * u / p - 1.0) : 0.0f;
    outs[1] = event ? 1.0f : 0.0f;
  }
  void copy_state_from(const Module& other) override {
    state_ = static_cast<const Dust&>(other).state_;
  }
 private:
  double rate_ = 48000;
  uint32_t state_ = 1;
};
std::unique_ptr<Module> make() { return std::make_unique<Dust>(); }
}  // namespace
const ModuleInfo dust_info{"dust", kIns, kCfg, kOuts, &make};
}  // namespace ms::dsp
