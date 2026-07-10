#include "dsp/modules.h"

namespace ms::dsp {
namespace {

constexpr InputInfo kIns[] = {{"in", 0.f}, {"left", 0.f}, {"right", 0.f}, {"level", 1.f}};
constexpr OutputInfo kOuts[] = {{"l"}, {"r"}};

class OutModule final : public Module {
 public:
  void process(const float* ins, float* outs) override {
    outs[0] = (ins[1] + ins[0]) * ins[3];
    outs[1] = (ins[2] + ins[0]) * ins[3];
  }
};

std::unique_ptr<Module> make() { return std::make_unique<OutModule>(); }
}  // namespace

const ModuleInfo out_info{"out", kIns, {}, kOuts, &make};
}  // namespace ms::dsp
