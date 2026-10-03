#include <algorithm>
#include <cmath>
#include <numbers>
#include "dsp/modules.h"

namespace ms::dsp {
namespace {

constexpr InputInfo kIns[] = {{"a", 0.f}, {"b", 0.f}, {"mix", 0.5f}};
constexpr const char* kLaws[] = {"linear", "equal_power", nullptr};
constexpr ConfigInfo kCfg[] = {{"law", kLaws}};
constexpr OutputInfo kOuts[] = {{"out"}};

class Xfade final : public Module {
 public:
  void init(double, const Config& cfg) override {
    power_ = cfg_str(cfg, "law", "linear") == "equal_power";
  }
  void process(const float* ins, float* outs) override {
    const float m = std::clamp(ins[2], 0.0f, 1.0f);
    if (power_) {
      const float angle = m * (std::numbers::pi_v<float> / 2.0f);
      outs[0] = ins[0] * std::cos(angle) + ins[1] * std::sin(angle);
    } else {
      outs[0] = ins[0] * (1.0f - m) + ins[1] * m;
    }
  }
 private:
  bool power_ = false;
};

std::unique_ptr<Module> make() { return std::make_unique<Xfade>(); }
}  // namespace

const ModuleInfo xfade_info{"xfade", kIns, kCfg, kOuts, &make};
}  // namespace ms::dsp
