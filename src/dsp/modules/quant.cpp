#include <cmath>
#include <vector>
#include "dsp/modules.h"

namespace ms::dsp {
namespace {

constexpr InputInfo kIns[] = {{"in", 0.f}, {"root", 0.f}, {"transpose", 0.f}};
constexpr ConfigInfo kCfg[] = {{"scale", nullptr}};
constexpr OutputInfo kOuts[] = {{"out"}, {"changed"}};

class Quant final : public Module {
 public:
  void init(double, const Config& cfg) override {
    degrees_.clear();
    bool present[12] = {};
    const auto& arr = cfg_arr(cfg, "scale");
    if (arr.empty()) {
      for (int i = 0; i < 12; ++i) present[i] = true;
    } else {
      for (double d : arr) {
        int s = static_cast<int>(std::lround(d)) % 12;
        if (s < 0) s += 12;
        present[s] = true;
      }
    }
    for (int i = 0; i < 12; ++i)
      if (present[i]) degrees_.push_back(i);
    first_ = true;
    last_q_ = 0;
  }

  void process(const float* ins, float* outs) override {
    const float semis = (ins[0] + ins[2]) * 12.0f - ins[1];
    const int base =
        static_cast<int>(std::floor(semis / 12.0f)) * 12;
    int best = base + degrees_.front();
    float bestd = 1e9f;
    for (int oct = base - 12; oct <= base + 12; oct += 12) {
      for (int d : degrees_) {
        const float dist = std::abs(static_cast<float>(oct + d) - semis);
        if (dist < bestd) {  // strict <: ascending scan means ties snap down
          bestd = dist;
          best = oct + d;
        }
      }
    }
    outs[0] = (static_cast<float>(best) + ins[1]) / 12.0f;
    outs[1] = (!first_ && best != last_q_) ? 1.0f : 0.0f;
    last_q_ = best;
    first_ = false;
  }

  void copy_state_from(const Module& other) override {
    const auto& o = static_cast<const Quant&>(other);
    last_q_ = o.last_q_;
    first_ = o.first_;
  }

 private:
  std::vector<int> degrees_;  // built in init (non-RT); read-only in process
  int last_q_ = 0;
  bool first_ = true;
};

std::unique_ptr<Module> make() { return std::make_unique<Quant>(); }
}  // namespace

const ModuleInfo quant_info{"quant", kIns, kCfg, kOuts, &make};
}  // namespace ms::dsp
