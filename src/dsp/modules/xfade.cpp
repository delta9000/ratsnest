#include <algorithm>
#include <cmath>
#include <numbers>
#include "dsp/modules.h"

namespace ms::dsp {
namespace {

constexpr InputInfo kIns[] = {{"a", 0.f}, {"b", 0.f}, {"mix", 0.5f}};
constexpr OutputInfo kOuts[] = {{"out"}};

class Xfade final : public Module {
 public:
  void process(const float* ins, float* outs) override {
    const float m = std::clamp(ins[2], 0.0f, 1.0f);
    const float angle = m * (std::numbers::pi_v<float> / 2.0f);
    outs[0] = ins[0] * std::cos(angle) + ins[1] * std::sin(angle);
  }
};

std::unique_ptr<Module> make() { return std::make_unique<Xfade>(); }
}  // namespace

const ModuleInfo xfade_info{"xfade", kIns, {}, kOuts, &make};
}  // namespace ms::dsp
