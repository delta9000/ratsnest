#include <algorithm>
#include <cmath>
#include "dsp/modules.h"
#include "dsp/util.h"

namespace ms::dsp {
namespace {
constexpr InputInfo kIns[] = {
    {"in1", 0.f}, {"in2", 0.f}, {"in3", 0.f}, {"in4", 0.f},
    {"in5", 0.f}, {"in6", 0.f}, {"in7", 0.f}, {"in8", 0.f},
    {"clock", 0.f, false}, {"reset", 0.f, false}, {"addr", 0.f, false}, {"n", 8.f, false}};
constexpr const char* kModes[] = {"seq", "addr", nullptr};
constexpr ConfigInfo kCfg[] = {{"mode", kModes}, {"fade", nullptr}};
// Default `fade` (seconds): a selection change slews out the step instead of
// cutting. `fade: 0` is the instant cut that CV and gate routing wants.
constexpr double kFade = 0.005;
constexpr OutputInfo kOuts[] = {{"out"}};

class Switch final : public Module {
 public:
  void init(double rate, const Config& cfg) override {
    addressed_ = cfg_str(cfg, "mode", "seq") == "addr";
    fade_ = static_cast<float>(clamp_nan(cfg_num(cfg, "fade", kFade), 0.0, 1.0) * rate);
  }
  void process(const float* ins, float* outs) override {
    const int n = round_clamp(ins[11], 1, 8, 8);
    const bool reset = reset_edge_(ins[9]);
    const bool rising = clock_edge_(ins[8]);
    if (reset) index_ = -1;
    if (addressed_) index_ = round_clamp(ins[10], 0, n - 1, 0);
    else if (rising) index_ = (index_ + 1) % n;
    const int selected = index_ < 0 ? 0 : std::min(index_, n - 1);
    if (selected != shown_) declick_.jump(out_, ins[selected], fade_);
    shown_ = selected;
    out_ = outs[0] = declick_(ins[selected]);
  }
  void copy_state_from(const Module& other) override {
    const auto& o = static_cast<const Switch&>(other);
    index_ = o.index_;
    clock_edge_ = o.clock_edge_;
    reset_edge_ = o.reset_edge_;
    shown_ = o.shown_;
    out_ = o.out_;
    declick_ = o.declick_;
  }
 private:
  bool addressed_ = false;
  int index_ = -1;
  int shown_ = 0;  // the input the output currently follows
  float fade_ = 0.0f;  // samples
  float out_ = 0.0f;
  Declick declick_;
  RisingEdge clock_edge_;
  RisingEdge reset_edge_;
};
std::unique_ptr<Module> make() { return std::make_unique<Switch>(); }
}  // namespace
const ModuleInfo switch_info{"switch", kIns, kCfg, kOuts, &make};
}  // namespace ms::dsp
