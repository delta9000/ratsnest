#include <algorithm>
#include <cmath>
#include <cstdint>
#include <numbers>
#include "dsp/modules.h"
#include "dsp/util.h"

namespace ms::dsp {
namespace {

constexpr InputInfo kIns[] = {{"rate", 1.f}, {"smooth", 1.f}};
constexpr ConfigInfo kCfg[] = {{"seed", nullptr}};
constexpr OutputInfo kOuts[] = {{"out"}, {"stepped"}};

class Random final : public Module {
 public:
  void init(double rate, const Config& cfg) override {
    rate_ = rate;
    state_ = cfg_seed(cfg);
    phase_ = 0.0f;
    prev_ = draw();
    next_ = draw();
  }

  void process(const float* ins, float* outs) override {
    phase_ += static_cast<double>(std::max(sanitize(ins[0]), 0.0f)) / rate_;
    if (phase_ >= 1.0f) {
      phase_ -= std::floor(phase_);
      prev_ = next_;
      next_ = draw();
    }
    const float sm = std::clamp(ins[1], 0.0f, 1.0f);
    const float eased =
        prev_ + (next_ - prev_) *
                    0.5f * (1.0f - std::cos(std::numbers::pi_v<float> * static_cast<float>(phase_)));
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
  float draw() { return 2.0f * unit_float(xorshift32(state_)) - 1.0f; }

  double rate_ = 48000.0;
  std::uint32_t state_ = 0x9E3779B9u;
  double phase_ = 0.0;
  float prev_ = 0.0f;
  float next_ = 0.0f;
};

std::unique_ptr<Module> make() { return std::make_unique<Random>(); }
}  // namespace

const ModuleInfo random_info{"random", kIns, kCfg, kOuts, &make};
}  // namespace ms::dsp
