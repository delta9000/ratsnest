#include <algorithm>
#include <cmath>
#include "dsp/modules.h"
#include "dsp/util.h"

namespace ms::dsp {
namespace {

constexpr InputInfo kIns[] = {{"in", 0.f}, {"by", 2.f}, {"reset", 0.f}};
constexpr OutputInfo kOuts[] = {{"out"}};

class DivModule final : public Module {
 public:
  void process(const float* ins, float* outs) override {
    const bool reset = reset_edge_(ins[2]);
    const bool rising = in_edge_(ins[0]);

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
    in_edge_ = o.in_edge_;
    reset_edge_ = o.reset_edge_;
  }

 private:
  int count_ = -1;
  float gate_ = 0.0f;
  RisingEdge in_edge_;
  RisingEdge reset_edge_;
};

std::unique_ptr<Module> make() { return std::make_unique<DivModule>(); }
}  // namespace

const ModuleInfo div_info{"div", kIns, {}, kOuts, &make};
}  // namespace ms::dsp
