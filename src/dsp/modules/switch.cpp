#include <algorithm>
#include <cmath>
#include "dsp/modules.h"

namespace ms::dsp {
namespace {
constexpr InputInfo kIns[] = {
    {"in1", 0.f}, {"in2", 0.f}, {"in3", 0.f}, {"in4", 0.f},
    {"in5", 0.f}, {"in6", 0.f}, {"in7", 0.f}, {"in8", 0.f},
    {"clock", 0.f, false}, {"reset", 0.f, false}, {"addr", 0.f, false}, {"n", 8.f, false}};
constexpr const char* kModes[] = {"seq", "addr", nullptr};
constexpr ConfigInfo kCfg[] = {{"mode", kModes}};
constexpr OutputInfo kOuts[] = {{"out"}};

int rounded(float value, int lo, int hi, int fallback) {
  if (!std::isfinite(value)) return fallback;
  return static_cast<int>(std::lround(std::clamp(
      value, static_cast<float>(lo), static_cast<float>(hi))));
}
class Switch final : public Module {
 public:
  void init(double, const Config& cfg) override {
    addressed_ = cfg_str(cfg, "mode", "seq") == "addr";
  }
  void process(const float* ins, float* outs) override {
    const int n = rounded(ins[11], 1, 8, 8);
    const bool reset = ins[9] > 0.5f && prev_reset_ <= 0.5f;
    const bool rising = ins[8] > 0.5f && prev_clock_ <= 0.5f;
    prev_reset_ = ins[9];
    prev_clock_ = ins[8];
    if (reset) index_ = -1;
    if (addressed_) index_ = rounded(ins[10], 0, n - 1, 0);
    else if (rising) index_ = (index_ + 1) % n;
    const int selected = index_ < 0 ? 0 : std::min(index_, n - 1);
    outs[0] = ins[selected];
  }
  void copy_state_from(const Module& other) override {
    const auto& o = static_cast<const Switch&>(other);
    index_ = o.index_;
    prev_clock_ = o.prev_clock_;
    prev_reset_ = o.prev_reset_;
  }
 private:
  bool addressed_ = false;
  int index_ = -1;
  float prev_clock_ = 0.0f;
  float prev_reset_ = 0.0f;
};
std::unique_ptr<Module> make() { return std::make_unique<Switch>(); }
}  // namespace
const ModuleInfo switch_info{"switch", kIns, kCfg, kOuts, &make};
}  // namespace ms::dsp
