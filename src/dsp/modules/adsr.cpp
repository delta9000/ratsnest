#include <algorithm>
#include <cmath>
#include "dsp/modules.h"
#include "dsp/util.h"

namespace ms::dsp {
namespace {

constexpr InputInfo kIns[] = {{"gate", 0.f},
                              {"attack", 0.005f},
                              {"decay", 0.15f},
                              {"sustain", 0.7f},
                              {"release", 0.3f}};
constexpr const char* kCurves[] = {"lin", "exp", "log", "quad", nullptr};
constexpr ConfigInfo kCfg[] = {{"curve", kCurves}};
constexpr OutputInfo kOuts[] = {{"out"}};

class Adsr final : public Module {
 public:
  void init(double rate, const Config& cfg) override {
    rate_ = rate;
    curve_ = cfg_enum(cfg, "curve", kCurves);
    stage_ = Stage::Idle;
    level_ = 0.0;
    start_ = 0.0;
    progress_ = 0.0;
    gate_edge_ = RisingEdge{};
  }

  void process(const float* ins, float* outs) override {
    const bool high = ins[0] > 0.5f;
    const bool triggered = gate_edge_(ins[0]);

    if (triggered) {
      start_ = level_;
      progress_ = 0.0;
      stage_ = Stage::Attack;
    } else if (!high && stage_ != Stage::Idle && stage_ != Stage::Release) {
      start_ = level_;
      progress_ = 0.0;
      stage_ = Stage::Release;
    }

    const double sus = std::clamp(static_cast<double>(ins[3]), 0.0, 1.0);
    switch (stage_) {
      case Stage::Attack:
        progress_ = std::min(1.0, progress_ + increment(ins[1]));
        level_ = start_ + (1.0 - start_) * shape(progress_, true);
        if (progress_ >= 1.0) {
          level_ = 1.0;
          start_ = 1.0;
          progress_ = 0.0;
          stage_ = Stage::Decay;
        }
        break;
      case Stage::Decay:
        progress_ = std::min(1.0, progress_ + increment(ins[2]));
        // Live sustain: the tail bends toward wherever sustain is now.
        level_ = sus + (1.0 - sus) * (1.0 - shape(progress_, false));
        if (progress_ >= 1.0) {
          level_ = sus;
          stage_ = Stage::Sustain;
        }
        break;
      case Stage::Sustain:
        level_ = sus;
        break;
      case Stage::Release:
        progress_ = std::min(1.0, progress_ + increment(ins[4]));
        level_ = start_ * (1.0 - shape(progress_, false));
        if (progress_ >= 1.0) {
          level_ = 0.0;
          stage_ = Stage::Idle;
        }
        break;
      case Stage::Idle:
        level_ = 0.0;
        break;
    }

    outs[0] = static_cast<float>(level_);
  }

  void copy_state_from(const Module& other) override {
    const auto& o = static_cast<const Adsr&>(other);
    stage_ = o.stage_;
    level_ = o.level_;
    start_ = o.start_;
    progress_ = o.progress_;
    gate_edge_ = o.gate_edge_;
  }

 private:
  enum class Stage { Idle, Attack, Decay, Sustain, Release };

  // progress_/level_ are double: see env_increment.
  double increment(float seconds) const { return env_increment(seconds, rate_); }
  double shape(double x, bool rising) const { return env_curve(curve_, x, rising); }

  double rate_ = 48000.0;
  int curve_ = 0;
  Stage stage_ = Stage::Idle;
  double level_ = 0.0;
  double start_ = 0.0;
  double progress_ = 0.0;
  RisingEdge gate_edge_;
};

std::unique_ptr<Module> make() { return std::make_unique<Adsr>(); }
}  // namespace

const ModuleInfo adsr_info{"adsr", kIns, kCfg, kOuts, &make};
}  // namespace ms::dsp
