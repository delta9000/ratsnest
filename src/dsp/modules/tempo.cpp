#include <algorithm>
#include <cmath>
#include "dsp/modules.h"

namespace ms::dsp {
namespace {
constexpr InputInfo kIns[] = {in_bpm("bpm", 120.f), {"beats", 1.f}};
constexpr OutputInfo kOuts[] = {{"seconds"}, {"hz"}};
class Tempo final : public Module {
 public:
  void process(const float* ins, float* outs) override {
    const float bpm = std::isfinite(ins[0]) ? std::clamp(ins[0], 0.001f, 1e6f) : 120.0f;
    const float beats = std::isfinite(ins[1]) ? std::clamp(ins[1], 0.0f, 1024.0f) : 1.0f;
    const float seconds = std::min(60.0f * beats / bpm, 86400.0f);
    outs[0] = seconds;
    outs[1] = seconds > 0.0f ? 1.0f / seconds : 0.0f;
  }
};
std::unique_ptr<Module> make() { return std::make_unique<Tempo>(); }
}  // namespace
const ModuleInfo tempo_info{"tempo", kIns, {}, kOuts, &make};
}  // namespace ms::dsp
