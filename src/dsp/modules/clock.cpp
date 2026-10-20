#include <algorithm>
#include <cmath>
#include "dsp/modules.h"
#include "dsp/util.h"

namespace ms::dsp {
namespace {

constexpr InputInfo kIns[] = {{"bpm", 120.f}, {"run", 1.f}, {"reset", 0.f}};
constexpr OutputInfo kOuts[] = {{"out"}, {"run"}};

class ClockModule final : public Module {
 public:
  void init(double rate, const Config&) override {
    rate_ = rate;
    phase_ = 0.0;
    reset_edge_ = RisingEdge{};
  }

  void process(const float* ins, float* outs) override {
    const bool reset = reset_edge_(ins[2]);
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
    reset_edge_ = o.reset_edge_;
  }

 private:
  double rate_ = 48000.0;
  double phase_ = 0.0;
  RisingEdge reset_edge_;
};

std::unique_ptr<Module> make() { return std::make_unique<ClockModule>(); }
}  // namespace

const ModuleInfo clock_info{"clock", kIns, {}, kOuts, &make};
}  // namespace ms::dsp
