#include <algorithm>
#include <cmath>
#include "dsp/modules.h"
#include "dsp/util.h"

namespace ms::dsp {
namespace {

constexpr InputInfo kIns[] = {
    {"trig", 0.f}, {"in", 0.f}, {"rise", 0.01f}, {"fall", 0.1f}};
constexpr const char* kCurves[] = {"lin", "exp", "log", nullptr};
constexpr const char* kCycle[] = {"off", "on", nullptr};
constexpr ConfigInfo kCfg[] = {{"curve", kCurves}, {"cycle", kCycle}};
constexpr OutputInfo kOuts[] = {{"out"}, {"eor"}, {"eoc"}};

class FuncModule final : public Module {
 public:
  void init(double rate, const Config& cfg) override {
    rate_ = rate;
    curve_ = cfg_enum(cfg, "curve", kCurves);
    cycle_ = cfg_str(cfg, "cycle", "off") == "on";
  }

  void process(const float* ins, float* outs) override {
    outs[1] = 0.0f;
    outs[2] = 0.0f;
    const bool triggered = trig_edge_(ins[0]);
    if (triggered) start_rise();

    if (stage_ == Stage::Rise) {
      progress_ = std::min(1.0, progress_ + increment(ins[2]));
      level_ = rise_start_ + (1.0 - rise_start_) * shape(progress_);
      if (progress_ >= 1.0) {
        level_ = 1.0;
        stage_ = Stage::Fall;
        progress_ = 0.0;
        outs[1] = 1.0f;
      }
    } else if (stage_ == Stage::Fall) {
      progress_ = std::min(1.0, progress_ + increment(ins[3]));
      level_ = 1.0 - shape(progress_);
      if (progress_ >= 1.0) {
        level_ = 0.0;
        outs[2] = 1.0f;
        if (cycle_) start_rise();
        else stage_ = Stage::Idle;
      }
    } else {
      level_ = std::clamp(static_cast<double>(ins[1]),
                          level_ - increment(ins[3]), level_ + increment(ins[2]));
    }

    outs[0] = static_cast<float>(level_);
  }

  void copy_state_from(const Module& other) override {
    const auto& o = static_cast<const FuncModule&>(other);
    stage_ = o.stage_;
    level_ = o.level_;
    rise_start_ = o.rise_start_;
    progress_ = o.progress_;
    trig_edge_ = o.trig_edge_;
  }

 private:
  enum class Stage { Idle, Rise, Fall };

  // progress_/level_ are double: see env_increment.
  double increment(float seconds) const { return env_increment(seconds, rate_); }
  double shape(double x) const { return env_curve(curve_, x); }

  void start_rise() {
    rise_start_ = level_;
    progress_ = 0.0;
    stage_ = Stage::Rise;
  }

  double rate_ = 48000.0;
  Stage stage_ = Stage::Idle;
  double level_ = 0.0;
  double rise_start_ = 0.0;
  double progress_ = 0.0;
  RisingEdge trig_edge_;
  int curve_ = 0;
  bool cycle_ = false;
};

std::unique_ptr<Module> make() { return std::make_unique<FuncModule>(); }
}  // namespace

const ModuleInfo func_info{"func", kIns, kCfg, kOuts, &make};
}  // namespace ms::dsp
