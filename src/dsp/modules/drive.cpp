#include <algorithm>
#include <cmath>
#include "dsp/modules.h"

namespace ms::dsp {
namespace {

constexpr InputInfo kIns[] = {{"in", 0.f}, {"amount", 0.5f}};
constexpr const char* kModes[] = {"soft", "hard", nullptr};
constexpr ConfigInfo kCfg[] = {{"mode", kModes}};
constexpr OutputInfo kOuts[] = {{"out"}};

class Drive final : public Module {
 public:
  void init(double, const Config& cfg) override {
    hard_ = cfg_str(cfg, "mode", "soft") == "hard";
  }

  void process(const float* ins, float* outs) override {
    const float g = 1.0f + 9.0f * std::clamp(ins[1], 0.0f, 1.0f);
    const float x = ins[0] * g;
    outs[0] = hard_ ? std::clamp(x, -1.0f, 1.0f) : std::tanh(x);
  }

 private:
  bool hard_ = false;
};

std::unique_ptr<Module> make() { return std::make_unique<Drive>(); }
}  // namespace

const ModuleInfo drive_info{"drive", kIns, kCfg, kOuts, &make};
}  // namespace ms::dsp
