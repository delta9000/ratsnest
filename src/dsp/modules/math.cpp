#include <algorithm>
#include <cmath>
#include "dsp/modules.h"

namespace ms::dsp {
namespace {

constexpr InputInfo kIns[] = {{"a", 0.f}, {"b", 0.f}};
constexpr const char* kOps[] = {"add", "sub", "mult", "min", "max",
                                "abs", "clip", "wrap", "exp2", nullptr};
constexpr ConfigInfo kCfg[] = {{"op", kOps}};
constexpr OutputInfo kOuts[] = {{"out"}};

class MathModule final : public Module {
 public:
  void init(double, const Config& cfg) override {
    op_ = cfg_enum(cfg, "op", kOps);
  }

  void process(const float* ins, float* outs) override {
    const float a = ins[0];
    const float b = ins[1];
    switch (op_) {
      case 0: outs[0] = a + b; break;
      case 1: outs[0] = a - b; break;
      case 2: outs[0] = a * b; break;
      case 3: outs[0] = std::min(a, b); break;
      case 4: outs[0] = std::max(a, b); break;
      case 5: outs[0] = std::abs(a); break;
      case 6: {
        const float r = std::abs(b);
        outs[0] = std::clamp(a, -r, r);
        break;
      }
      case 7: {
        const float r = std::abs(b);
        if (r <= 0.0f) { outs[0] = 0.0f; break; }
        float x = std::fmod(a + r, 2.0f * r);
        if (x < 0.0f) x += 2.0f * r;
        outs[0] = x - r;
        break;
      }
      case 8: outs[0] = std::isfinite(a) ? std::exp2(std::clamp(a, -126.0f, 126.0f)) : 0.0f; break;
    }
  }

 private:
  int op_ = 0;
};

std::unique_ptr<Module> make() { return std::make_unique<MathModule>(); }
}  // namespace

const ModuleInfo math_info{"math", kIns, kCfg, kOuts, &make};
}  // namespace ms::dsp
