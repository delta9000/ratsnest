#include <algorithm>
#include <cmath>
#include "dsp/modules.h"

namespace ms::dsp {
namespace {

constexpr InputInfo kIns[] = {{"in", 0.f}, {"fold", 1.f}, {"sym", 0.f}};
constexpr const char* kQualities[] = {"raw", "clean", nullptr};
constexpr ConfigInfo kCfg[] = {{"quality", kQualities}};
constexpr OutputInfo kOuts[] = {{"out"}};

class Fold final : public Module {
 public:
  void init(double, const Config& cfg) override {
    clean_ = cfg_str(cfg, "quality", "raw") == "clean";
  }

  void process(const float* ins, float* outs) override {
    const float x = ins[0] * std::max(ins[1], 0.0f) + ins[2];
    float t = (x + 1.0f) * 0.25f;
    t -= std::floor(t);
    outs[0] = 1.0f - 4.0f * std::abs(t - 0.5f);
  }

 private:
  bool clean_ = false;
};

std::unique_ptr<Module> make() { return std::make_unique<Fold>(); }
}  // namespace

const ModuleInfo fold_info{"fold", kIns, kCfg, kOuts, &make};
}  // namespace ms::dsp
