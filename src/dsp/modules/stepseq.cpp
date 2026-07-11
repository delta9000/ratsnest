#include <algorithm>
#include <cmath>
#include <cstdint>
#include <vector>
#include "dsp/modules.h"

namespace ms::dsp {
namespace {
constexpr InputInfo kIns[] = {{"clock", 0.f}, {"reset", 0.f},
                              {"len", 0.f}, {"transpose", 0.f}};
constexpr const char* kDirs[] = {"forward", "reverse", "pingpong", "random", nullptr};
constexpr ConfigInfo kCfg[] = {{"steps", nullptr}, {"gates", nullptr},
                               {"dir", kDirs}, {"seed", nullptr}};
constexpr OutputInfo kOuts[] = {{"cv"}, {"trig"}, {"gate"}, {"eoc"}};
class StepSeq final : public Module {
 public:
  void init(double, const Config& cfg) override {
    steps_ = cfg_arr(cfg, "steps");
    gates_ = cfg_arr(cfg, "gates");
    if (steps_.empty()) steps_.push_back(0.0);
    const std::string dir = cfg_str(cfg, "dir", "forward");
    for (int i = 0; kDirs[i]; ++i) if (dir == kDirs[i]) dir_ = i;
    double seed = cfg_num(cfg, "seed", 1.0);
    if (!std::isfinite(seed)) seed = 1.0;
    rng_ = static_cast<std::uint32_t>(
               std::clamp(std::abs(seed), 0.0, 4294967295.0)) * 2654435761u;
    if (rng_ == 0) rng_ = 0x9E3779B9u;
  }
  void process(const float* ins, float* outs) override {
    int len = static_cast<int>(steps_.size());
    if (std::isfinite(ins[2]) && ins[2] > 0.0f)
      len = static_cast<int>(std::lround(std::clamp(
          ins[2], 1.0f, static_cast<float>(steps_.size()))));
    const bool reset = ins[1] > 0.5f && prev_reset_ <= 0.5f;
    const bool rising = ins[0] > 0.5f && prev_clock_ <= 0.5f;
    prev_reset_ = ins[1]; prev_clock_ = ins[0];
    if (reset) { index_ = -1; direction_ = 1; active_ = false; }
    bool trig = false, eoc = false;
    if (rising) {
      const int old = index_;
      if (dir_ == 0) index_ = (index_ + 1) % len;
      else if (dir_ == 1) index_ = index_ < 0 ? len - 1 : (index_ + len - 1) % len;
      else if (dir_ == 2) {
        if (len == 1) { index_ = 0; direction_ = 1; }
        else if (index_ < 0) index_ = 0;
        else {
          if (index_ + direction_ >= len || index_ + direction_ < 0) direction_ = -direction_;
          index_ += direction_;
        }
      } else index_ = static_cast<int>(draw() % static_cast<std::uint32_t>(len));
      eoc = old >= 0 && ((dir_ == 0 && index_ == 0) ||
                         (dir_ == 1 && index_ == len - 1));
      active_ = index_ >= static_cast<int>(gates_.size()) || gates_[static_cast<size_t>(index_)] > 0.5;
      trig = active_;
    }
    const int current = index_ < 0 ? 0 : index_;
    outs[0] = static_cast<float>(steps_[static_cast<size_t>(current)]) + ins[3];
    outs[1] = trig ? 1.0f : 0.0f;
    outs[2] = active_ && ins[0] > 0.5f ? 1.0f : 0.0f;
    outs[3] = eoc ? 1.0f : 0.0f;
  }
  void copy_state_from(const Module& other) override {
    const auto& o = static_cast<const StepSeq&>(other);
    rng_ = o.rng_; index_ = o.index_; direction_ = o.direction_;
    active_ = o.active_; prev_clock_ = o.prev_clock_; prev_reset_ = o.prev_reset_;
  }
 private:
  std::uint32_t draw() { std::uint32_t x = rng_; x ^= x << 13; x ^= x >> 17; x ^= x << 5; return rng_ = x; }
  std::vector<double> steps_;
  std::vector<double> gates_;
  std::uint32_t rng_ = 0x9E3779B9u;
  int dir_ = 0, index_ = -1, direction_ = 1;
  bool active_ = false;
  float prev_clock_ = 0.0f, prev_reset_ = 0.0f;
};
std::unique_ptr<Module> make() { return std::make_unique<StepSeq>(); }
}  // namespace
const ModuleInfo stepseq_info{"stepseq", kIns, kCfg, kOuts, &make};
}  // namespace ms::dsp
