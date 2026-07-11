#include <algorithm>
#include <cmath>
#include <numbers>
#include "dsp/modules.h"

namespace ms::dsp {
namespace {

constexpr InputInfo kIns[] = {
    {"in1", 0.f}, {"in2", 0.f}, {"in3", 0.f}, {"in4", 0.f},
    {"in5", 0.f}, {"in6", 0.f}, {"in7", 0.f}, {"in8", 0.f},
    {"lvl1", 0.f}, {"lvl2", 0.f}, {"lvl3", 0.f}, {"lvl4", 0.f},
    {"lvl5", 0.f}, {"lvl6", 0.f}, {"lvl7", 0.f}, {"lvl8", 0.f}};
constexpr ConfigInfo kCfg[] = {{"levels", nullptr}, {"pans", nullptr}};
constexpr OutputInfo kOuts[] = {{"out_l"}, {"out_r"}};
constexpr int kCh = 8;

class Mix final : public Module {
 public:
  void init(double, const Config& cfg) override {
    const auto& levels = cfg_arr(cfg, "levels");
    const auto& pans = cfg_arr(cfg, "pans");
    for (int i = 0; i < kCh; ++i) {
      base_[i] = i < static_cast<int>(levels.size())
                     ? static_cast<float>(levels[static_cast<size_t>(i)])
                     : 1.0f;
      const float pan =
          i < static_cast<int>(pans.size())
              ? std::clamp(static_cast<float>(pans[static_cast<size_t>(i)]),
                           -1.0f, 1.0f)
              : 0.0f;
      const float angle = (pan + 1.0f) * (std::numbers::pi_v<float> / 4.0f);
      gl_[i] = std::cos(angle);
      gr_[i] = std::sin(angle);
    }
  }

  void process(const float* ins, float* outs) override {
    float l = 0.0f, r = 0.0f;
    for (int i = 0; i < kCh; ++i) {
      const float g = std::clamp(base_[i] + ins[kCh + i], 0.0f, 8.0f);
      const float v = ins[i] * g;
      l += v * gl_[i];
      r += v * gr_[i];
    }
    outs[0] = l;
    outs[1] = r;
  }

 private:
  float base_[kCh] = {};
  float gl_[kCh] = {};
  float gr_[kCh] = {};
};

std::unique_ptr<Module> make() { return std::make_unique<Mix>(); }
}  // namespace

const ModuleInfo mix_info{"mix", kIns, kCfg, kOuts, &make};
}  // namespace ms::dsp
