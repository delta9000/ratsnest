#include <algorithm>
#include <cmath>
#include <cstdint>
#include "dsp/modules.h"
#include "dsp/util.h"

namespace ms::dsp {
namespace {
constexpr InputInfo kIns[] = {in_hz("rate", 10.f)};
constexpr ConfigInfo kCfg[] = {{"seed", nullptr}};
constexpr OutputInfo kOuts[] = {{"out"}, {"trig"}};

class Dust final : public Module {
 public:
  void init(double rate, const Config& cfg) override {
    rate_ = rate;
    state_ = cfg_seed(cfg);
    hold_ = samples_per_48k(rate);
    // The click's area, amplitude x duration, fixed at one 48 kHz sample.
    area_ = static_cast<float>(rate / (48000.0 * hold_));
    left_ = 0;
  }
  void process(const float* ins, float* outs) override {
    // An impulse lasts one 48 kHz sample (util.h), so a click has the same
    // energy at every rate: 1 sample at 48 kHz and below (scaled down below
    // 48 kHz), 2 at 96 kHz.
    if (left_ > 0) {
      --left_;
      outs[0] = last_;
      outs[1] = 1.0f;
      return;
    }
    // One Bernoulli trial per sample, with a uniform bipolar amplitude
    // conditional on an event.
    const double u = static_cast<double>(xorshift32(state_)) / 4294967296.0;
    const double p = std::isfinite(ins[0]) ? std::clamp(ins[0] / rate_, 0.0, 1.0) : 0.0;
    const bool event = u < p;
    outs[0] = event ? area_ * static_cast<float>(2.0 * u / p - 1.0) : 0.0f;
    outs[1] = event ? 1.0f : 0.0f;
    if (event) left_ = hold_ - 1, last_ = outs[0];
  }
  void copy_state_from(const Module& other) override {
    const auto& o = static_cast<const Dust&>(other);
    state_ = o.state_;
    left_ = std::min(o.left_, hold_ - 1);
    last_ = o.last_;
  }
 private:
  double rate_ = 48000;
  uint32_t state_ = 1;
  int hold_ = 1, left_ = 0;
  float area_ = 1.0f;
  float last_ = 0.0f;
};
std::unique_ptr<Module> make() { return std::make_unique<Dust>(); }
}  // namespace
const ModuleInfo dust_info{"dust", kIns, kCfg, kOuts, &make};
}  // namespace ms::dsp
