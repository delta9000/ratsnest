#include <cmath>
#include <numbers>
#include "dsp/input_key.h"
#include "dsp/modules.h"

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
    const std::string w = cfg_str(cfg, "wave", "sine");
    wave_ = w == "tri" ? 1 : w == "saw" ? 2 : w == "square" ? 3 : 0;
    phase_ = 0.0f;
    prev_reset_ = 0.0f;
  }

  void process(const float* ins, float* outs) override {
    if (ins[1] > 0.5f && prev_reset_ <= 0.5f) phase_ = 0.0f;
    prev_reset_ = ins[1];

    double phase = phase_ + static_cast<double>(ins[2]);
    phase -= std::floor(phase);
    const float p = static_cast<float>(phase);

    switch (wave_) {
      case 0: outs[0] = std::sin(2.0f * std::numbers::pi_v<float> * p); break;
      case 1: outs[0] = 4.0f * std::abs(p - 0.5f) - 1.0f; break;
      case 2: outs[0] = 2.0f * p - 1.0f; break;
      case 3: outs[0] = p < 0.5f ? 1.0f : -1.0f; break;
    }

    if (freq_key_.changed({ins[0]}))  // skip the division at a held rate
      inc_ = static_cast<double>(std::max(ins[0], 0.0f)) / rate_;
    phase_ += inc_;
    phase_ -= std::floor(phase_);
  }

  void copy_state_from(const Module& other) override {
    const auto& o = static_cast<const Lfo&>(other);
    phase_ = o.phase_;
    prev_reset_ = o.prev_reset_;
  }

 private:
  double rate_ = 48000.0;
  int wave_ = 0;
  double phase_ = 0.0;
  float prev_reset_ = 0.0f;
  InputKey<1> freq_key_;
  double inc_ = 0.0;
};

std::unique_ptr<Module> make() { return std::make_unique<Lfo>(); }
}  // namespace

const ModuleInfo lfo_info{"lfo", kIns, kCfg, kOuts, &make};
}  // namespace ms::dsp
