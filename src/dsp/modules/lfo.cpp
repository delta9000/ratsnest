#include <cmath>
#include <numbers>
#include "dsp/modules.h"
#include "dsp/util.h"

namespace ms::dsp {
namespace {

constexpr InputInfo kIns[] = {{"freq", 1.f}, {"reset", 0.f}, {"phase", 0.f}};
constexpr const char* kWaves[] = {"sine", "tri", "saw", "square", nullptr};
constexpr ConfigInfo kCfg[] = {{"wave", kWaves}};
constexpr OutputInfo kOuts[] = {{"out"}};

class Lfo final : public Module {
 public:
  void init(double rate, const Config& cfg) override {
    rate_ = rate;
    wave_ = cfg_enum(cfg, "wave", kWaves);
    phase_ = 0.0f;
    reset_edge_ = RisingEdge{};
  }

  void process(const float* ins, float* outs) override {
    if (reset_edge_(ins[1])) phase_ = 0.0f;

    double phase = phase_ + static_cast<double>(ins[2]);
    phase -= std::floor(phase);
    const float p = static_cast<float>(phase);

    switch (wave_) {
      case 0: outs[0] = std::sin(2.0f * std::numbers::pi_v<float> * p); break;
      case 1: outs[0] = 4.0f * std::abs(p - 0.5f) - 1.0f; break;
      case 2: outs[0] = 2.0f * p - 1.0f; break;
      case 3: outs[0] = p < 0.5f ? 1.0f : -1.0f; break;
    }

    phase_ += static_cast<double>(std::max(ins[0], 0.0f)) / rate_;
    phase_ -= std::floor(phase_);
  }

  void copy_state_from(const Module& other) override {
    const auto& o = static_cast<const Lfo&>(other);
    phase_ = o.phase_;
    reset_edge_ = o.reset_edge_;
  }

 private:
  double rate_ = 48000.0;
  int wave_ = 0;
  double phase_ = 0.0;
  RisingEdge reset_edge_;
};

std::unique_ptr<Module> make() { return std::make_unique<Lfo>(); }
}  // namespace

const ModuleInfo lfo_info{"lfo", kIns, kCfg, kOuts, &make};
}  // namespace ms::dsp
