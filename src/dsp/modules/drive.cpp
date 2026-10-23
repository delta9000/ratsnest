#include <algorithm>
#include <cmath>
#include "dsp/modules.h"
#include "dsp/nonlinear_oversample.h"

namespace ms::dsp {
namespace {

constexpr InputInfo kIns[] = {{"in", 0.f}, {"amount", 0.5f}};
constexpr const char* kModes[] = {"soft", "hard", nullptr};
constexpr const char* kQualities[] = {"raw", "clean", nullptr};
constexpr ConfigInfo kCfg[] = {{"mode", kModes}, {"quality", kQualities}};
constexpr OutputInfo kOuts[] = {{"out"}};

class Drive final : public Module {
 public:
  void init(double, const Config& cfg) override {
    hard_ = cfg_str(cfg, "mode", "soft") == "hard";
    clean_ = cfg_str(cfg, "quality", "raw") == "clean";
    clean_oversampler_.reset();
  }

  void process(const float* ins, float* outs) override {
    if (!clean_) {
      const float g = 1.0f + 9.0f * std::clamp(ins[1], 0.0f, 1.0f);
      const float x = ins[0] * g;
      outs[0] = hard_ ? std::clamp(x, -1.0f, 1.0f) : std::tanh(x);
      return;
    }

    const float input = std::isfinite(ins[0]) ? ins[0] : 0.0f;
    const float amount = std::isfinite(ins[1])
        ? std::clamp(ins[1], 0.0f, 1.0f)
        : 0.0f;
    const float scaled = input * (1.0f + 9.0f * amount);
    const float x = std::isfinite(scaled) ? scaled : 0.0f;
    outs[0] = clean_oversampler_.process(x, [this](float high_rate) {
      return hard_ ? std::clamp(high_rate, -1.0f, 1.0f)
                   : std::tanh(high_rate);
    });
  }

  void copy_state_from(const Module& other) override {
    const auto& o = static_cast<const Drive&>(other);
    if (clean_ && o.clean_ && hard_ == o.hard_)
      clean_oversampler_ = o.clean_oversampler_;
  }

 private:
  bool hard_ = false;
  bool clean_ = false;
  NonlinearOversampler<8> clean_oversampler_{};
};

std::unique_ptr<Module> make() { return std::make_unique<Drive>(); }
}  // namespace

const ModuleInfo drive_info{"drive", kIns, kCfg, kOuts, &make};
}  // namespace ms::dsp
