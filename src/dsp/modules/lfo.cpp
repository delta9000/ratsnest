#include <cmath>
#include <numbers>
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
    rate_ = static_cast<float>(rate);
    const std::string w = cfg_str(cfg, "wave", "sine");
    wave_ = w == "tri" ? 1 : w == "saw" ? 2 : w == "square" ? 3 : 0;
    phase_ = 0.0f;
    prev_reset_ = 0.0f;
  }

  void process(const float* ins, float* outs) override {
    if (ins[1] > 0.5f && prev_reset_ <= 0.5f) phase_ = 0.0f;
    prev_reset_ = ins[1];

    float p = phase_ + ins[2];
    p -= std::floor(p);

    switch (wave_) {
      case 0: outs[0] = std::sin(2.0f * std::numbers::pi_v<float> * p); break;
      case 1: outs[0] = 4.0f * std::abs(p - 0.5f) - 1.0f; break;
      case 2: outs[0] = 2.0f * p - 1.0f; break;
      case 3: outs[0] = p < 0.5f ? 1.0f : -1.0f; break;
    }

    phase_ += std::max(ins[0], 0.0f) / rate_;
    phase_ -= std::floor(phase_);
  }

 private:
  float rate_ = 48000.0f;
  int wave_ = 0;
  float phase_ = 0.0f;
  float prev_reset_ = 0.0f;
};

std::unique_ptr<Module> make() { return std::make_unique<Lfo>(); }
}  // namespace

const ModuleInfo lfo_info{"lfo", kIns, kCfg, kOuts, &make};
}  // namespace ms::dsp
