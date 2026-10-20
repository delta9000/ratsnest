#include <algorithm>
#include <cmath>
#include "dsp/input_key.h"
#include "dsp/modules.h"

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
    const std::string curve = cfg_str(cfg, "curve", "lin");
    curve_ = curve == "exp" ? 1 : curve == "log" ? 2 : 0;
    cycle_ = cfg_str(cfg, "cycle", "off") == "on";
  }

  void process(const float* ins, float* outs) override {
    outs[1] = 0.0f;
    outs[2] = 0.0f;
    const bool triggered = ins[0] > 0.5f && prev_trig_ <= 0.5f;
    prev_trig_ = ins[0];
    if (triggered) start_rise();

    if (stage_ == Stage::Rise) {
      progress_ = std::min(1.0, progress_ + rise_increment(ins[2]));
      level_ = rise_start_ + (1.0 - rise_start_) * shape(progress_);
      if (progress_ >= 1.0) {
        level_ = 1.0;
        stage_ = Stage::Fall;
        progress_ = 0.0;
        outs[1] = 1.0f;
      }
    } else if (stage_ == Stage::Fall) {
      progress_ = std::min(1.0, progress_ + fall_increment(ins[3]));
      level_ = 1.0 - shape(progress_);
      if (progress_ >= 1.0) {
        level_ = 0.0;
        outs[2] = 1.0f;
        if (cycle_) start_rise();
        else stage_ = Stage::Idle;
      }
    } else {
      level_ = std::clamp(static_cast<double>(ins[1]),
                          level_ - fall_increment(ins[3]), level_ + rise_increment(ins[2]));
    }

    outs[0] = static_cast<float>(level_);
  }

  void copy_state_from(const Module& other) override {
    const auto& o = static_cast<const FuncModule&>(other);
    stage_ = o.stage_;
    level_ = o.level_;
    rise_start_ = o.rise_start_;
    progress_ = o.progress_;
    prev_trig_ = o.prev_trig_;
  }

 private:
  enum class Stage { Idle, Rise, Fall };

  // progress_/level_ accumulate per-sample increments; in float the increment
  // drops below half a ULP once seconds * rate exceeds 2^25 (~11.6 min at
  // 48 kHz) and the envelope freezes short of completion, so they are double.
  double increment(float seconds) const {
    const double samples = std::max(seconds, 0.0f) * rate_;
    return samples <= 1.0 ? 1.0 : 1.0 / samples;
  }

  // The division above, memoized while the time input holds still.
  double rise_increment(float seconds) {
    if (rise_key_.changed({seconds})) rise_inc_ = increment(seconds);
    return rise_inc_;
  }
  double fall_increment(float seconds) {
    if (fall_key_.changed({seconds})) fall_inc_ = increment(seconds);
    return fall_inc_;
  }

  double shape(double x) const {
    if (curve_ == 1) return x * x;
    if (curve_ == 2) return std::sqrt(x);
    return x;
  }

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
  float prev_trig_ = 0.0f;
  int curve_ = 0;
  bool cycle_ = false;
  InputKey<1> rise_key_, fall_key_;
  double rise_inc_ = 1.0, fall_inc_ = 1.0;
};

std::unique_ptr<Module> make() { return std::make_unique<FuncModule>(); }
}  // namespace

const ModuleInfo func_info{"func", kIns, kCfg, kOuts, &make};
}  // namespace ms::dsp
