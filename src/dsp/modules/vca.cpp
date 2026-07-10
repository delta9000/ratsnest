#include <algorithm>
#include "dsp/modules.h"

namespace ms::dsp {
namespace {

constexpr InputInfo kIns[] = {{"in", 0.f}, {"cv", 0.f}, {"gain", 1.f}};
constexpr const char* kResp[] = {"lin", "exp", nullptr};
constexpr ConfigInfo kCfg[] = {{"response", kResp}};
constexpr OutputInfo kOuts[] = {{"out"}};

class Vca final : public Module {
 public:
  void init(double, const Config& cfg) override {
    exp_ = cfg_str(cfg, "response", "lin") == "exp";
  }
  void process(const float* ins, float* outs) override {
    float g = std::clamp(ins[2] + ins[1], 0.0f, 8.0f);
    if (exp_) g *= g;
    outs[0] = ins[0] * g;
  }
 private:
  bool exp_ = false;
};

std::unique_ptr<Module> make() { return std::make_unique<Vca>(); }
}  // namespace

const ModuleInfo vca_info{"vca", kIns, kCfg, kOuts, &make};
}  // namespace ms::dsp
