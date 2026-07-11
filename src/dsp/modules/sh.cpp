#include "dsp/modules.h"

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
    prev_trig_ = 0.0f;
  }

  void process(const float* ins, float* outs) override {
    if (track_) {
      if (ins[1] > 0.5f) held_ = ins[0];
    } else {
      if (ins[1] > 0.5f && prev_trig_ <= 0.5f) held_ = ins[0];
    }
    prev_trig_ = ins[1];
    outs[0] = held_;
  }

  void copy_state_from(const Module& other) override {
    const auto& o = static_cast<const Sh&>(other);
    held_ = o.held_;
    prev_trig_ = o.prev_trig_;
  }

 private:
  bool track_ = false;
  float held_ = 0.0f;
  float prev_trig_ = 0.0f;
};

std::unique_ptr<Module> make() { return std::make_unique<Sh>(); }
}  // namespace

const ModuleInfo sh_info{"sh", kIns, kCfg, kOuts, &make};
}  // namespace ms::dsp
