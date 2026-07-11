#include <algorithm>
#include <cmath>
#include "dsp/modules.h"

namespace ms::dsp {
namespace {

constexpr InputInfo kIns[] = {{"in", 0.f}, {"by", 2.f}, {"reset", 0.f}};
constexpr OutputInfo kOuts[] = {{"out"}};

class DivModule final : public Module {
 public:
  void process(const float* ins, float* outs) override {
    const bool reset = ins[2] > 0.5f && prev_reset_ <= 0.5f;
    prev_reset_ = ins[2];
    const bool rising = ins[0] > 0.5f && prev_in_ <= 0.5f;
    prev_in_ = ins[0];

    if (reset) {
      count_ = -1;
      gate_ = 0.0f;
    }
    if (rising) {
      const int by = std::max(1, static_cast<int>(std::lround(ins[1])));
      count_ = (count_ + 1) % by;
      gate_ = count_ < (by + 1) / 2 ? 1.0f : 0.0f;
    }
    outs[0] = gate_;
  }

  void copy_state_from(const Module& other) override {
    const auto& o = static_cast<const DivModule&>(other);
    count_ = o.count_;
    gate_ = o.gate_;
    prev_in_ = o.prev_in_;
    prev_reset_ = o.prev_reset_;
  }

 private:
  int count_ = -1;
  float gate_ = 0.0f;
  float prev_in_ = 0.0f;
  float prev_reset_ = 0.0f;
};

std::unique_ptr<Module> make() { return std::make_unique<DivModule>(); }
}  // namespace

const ModuleInfo div_info{"div", kIns, {}, kOuts, &make};
}  // namespace ms::dsp
