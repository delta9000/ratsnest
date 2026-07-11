#include <algorithm>
#include <cmath>
#include "dsp/modules.h"

namespace ms::dsp {
namespace {

constexpr InputInfo kIns[] = {{"bpm", 120.f}, {"run", 1.f}, {"reset", 0.f}};
constexpr OutputInfo kOuts[] = {{"out"}, {"run"}};

class ClockModule final : public Module {
 public:
  void init(double rate, const Config&) override {
    rate_ = rate;
    phase_ = 0.0;
    prev_reset_ = 0.0f;
  }

  void process(const float* ins, float* outs) override {
    const bool reset = ins[2] > 0.5f && prev_reset_ <= 0.5f;
    prev_reset_ = ins[2];
    if (reset) phase_ = 0.0;

    const bool running = ins[1] > 0.5f;
    outs[0] = running && phase_ < 0.5 ? 1.0f : 0.0f;
    outs[1] = running ? 1.0f : 0.0f;
    if (!running) return;

    const double bpm = std::max(static_cast<double>(ins[0]), 0.0);
    phase_ += bpm / (60.0 * rate_);
    phase_ -= std::floor(phase_);
  }

  void copy_state_from(const Module& other) override {
    const auto& o = static_cast<const ClockModule&>(other);
    phase_ = o.phase_;
    prev_reset_ = o.prev_reset_;
  }

 private:
  double rate_ = 48000.0;
  double phase_ = 0.0;
  float prev_reset_ = 0.0f;
};

std::unique_ptr<Module> make() { return std::make_unique<ClockModule>(); }
}  // namespace

const ModuleInfo clock_info{"clock", kIns, {}, kOuts, &make};
}  // namespace ms::dsp
