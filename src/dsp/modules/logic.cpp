#include "dsp/modules.h"

namespace ms::dsp {
namespace {
constexpr InputInfo kIns[] = {{"a", 0.f}, {"b", 0.f}, {"threshold", 0.5f}};
constexpr const char* kOps[] = {"and", "or", "xor", "not", "cmp", nullptr};
constexpr ConfigInfo kCfg[] = {{"op", kOps}};
constexpr OutputInfo kOuts[] = {{"out"}};

class Logic final : public Module {
 public:
  void init(double, const Config& cfg) override {
    const std::string op = cfg_str(cfg, "op", "and");
    for (int i = 0; kOps[i]; ++i) if (op == kOps[i]) op_ = i;
  }
  void process(const float* ins, float* outs) override {
    const bool a = ins[0] > ins[2];
    const bool b = ins[1] > ins[2];
    bool result = false;
    switch (op_) {
      case 0: result = a && b; break;
      case 1: result = a || b; break;
      case 2: result = a != b; break;
      case 3: result = !a; break;
      case 4: result = ins[0] > ins[1]; break;
    }
    outs[0] = result ? 1.0f : 0.0f;
  }
 private:
  int op_ = 0;
};
std::unique_ptr<Module> make() { return std::make_unique<Logic>(); }
}  // namespace
const ModuleInfo logic_info{"logic", kIns, kCfg, kOuts, &make};
}  // namespace ms::dsp
