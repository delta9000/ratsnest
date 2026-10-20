#include "dsp/modules.h"
#include "dsp/util.h"

namespace ms::dsp {
namespace {

constexpr InputInfo kIns[] = {{"in", 0.f}, {"trig", 0.f}};
constexpr const char* kModes[] = {"sample", "track", nullptr};
constexpr ConfigInfo kCfg[] = {{"mode", kModes}};
constexpr OutputInfo kOuts[] = {{"out"}};

class Sh final : public Module {
 public:
  void init(double, const Config& cfg) override {
    track_ = cfg_str(cfg, "mode", "sample") == "track";
    held_ = 0.0f;
    trig_edge_ = RisingEdge{};
  }

  void process(const float* ins, float* outs) override {
    const bool rose = trig_edge_(ins[1]);
    if (track_ ? ins[1] > 0.5f : rose) held_ = ins[0];
    outs[0] = held_;
  }

  void copy_state_from(const Module& other) override {
    const auto& o = static_cast<const Sh&>(other);
    held_ = o.held_;
    trig_edge_ = o.trig_edge_;
  }

 private:
  bool track_ = false;
  float held_ = 0.0f;
  RisingEdge trig_edge_;
};

std::unique_ptr<Module> make() { return std::make_unique<Sh>(); }
}  // namespace

const ModuleInfo sh_info{"sh", kIns, kCfg, kOuts, &make};
}  // namespace ms::dsp
