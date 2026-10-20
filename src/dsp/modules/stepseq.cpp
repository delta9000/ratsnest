#include <algorithm>
#include <cmath>
#include <cstdint>
#include <limits>
#include <vector>
#include "dsp/modules.h"
#include "dsp/util.h"

namespace ms::dsp {
namespace {
constexpr InputInfo kIns[] = {{"clock", 0.f}, {"reset", 0.f},
                              {"len", 0.f}, {"transpose", 0.f}};
constexpr const char* kDirs[] = {"forward", "reverse", "pingpong", "random", nullptr};
constexpr ConfigInfo kCfg[] = {{"steps", nullptr, ConfigKind::Numbers}, {"gates", nullptr, ConfigKind::Numbers},
                               {"lens", nullptr, ConfigKind::Numbers}, {"ties", nullptr, ConfigKind::Numbers},
                               {"vels", nullptr, ConfigKind::Numbers},
                               {"dir", kDirs}, {"seed", nullptr}};
constexpr OutputInfo kOuts[] = {{"cv"}, {"trig"}, {"gate"}, {"eoc"}, {"vel"}};
// A step sequencer with note values: every step lasts `lens[i]` clock ticks
// (missing = 1), a step marked in `ties` continues the previous note without
// retriggering (the gate never drops between them -- a slur when the pitch
// changes, a tie when it does not), and `vels` gives each step a held level
// on `vel`. The three arrays are parallel to `steps`, like `gates`.
class StepSeq final : public Module {
 public:
  void init(double, const Config& cfg) override {
    steps_ = cfg_arr(cfg, "steps");
    gates_ = cfg_arr(cfg, "gates");
    lens_ = cfg_arr(cfg, "lens");
    ties_ = cfg_arr(cfg, "ties");
    vels_ = cfg_arr(cfg, "vels");
    if (steps_.empty())
      steps_.resize(std::max({size_t{1}, gates_.size(), lens_.size(),
                             ties_.size(), vels_.size()}), 0.0);
    for (double& l : lens_)
      l = std::isfinite(l) ? std::clamp(std::round(l), 1.0,
          static_cast<double>(std::numeric_limits<int>::max())) : 1.0;
    dir_ = cfg_enum(cfg, "dir", kDirs);
    rng_ = cfg_seed(cfg);
  }
  void process(const float* ins, float* outs) override {
    const int len = live_len(ins[2]);
    const bool reset = reset_edge_(ins[1]);
    const bool rising = clock_edge_(ins[0]);
    if (reset) { index_ = -1; direction_ = 1; active_ = false; tick_ = 0; }
    clamp_index(len);
    bool trig = false, eoc = false;
    // A step longer than one tick simply counts ticks before advancing.
    if (rising && index_ >= 0 && tick_ + 1 < step_len(index_)) ++tick_;
    else if (rising) advance(len, trig, eoc);
    const int current = index_ < 0 ? 0 : index_;
    outs[0] = static_cast<float>(steps_[static_cast<size_t>(current)]) + ins[3];
    outs[1] = as_gate(trig);
    outs[2] = as_gate(active_ && (ins[0] > 0.5f || sustain(current, len)));
    outs[3] = as_gate(eoc);
    outs[4] = static_cast<float>(
        current < static_cast<int>(vels_.size()) ? vels_[static_cast<size_t>(current)] : 1.0);
  }
  void copy_state_from(const Module& other) override {
    const auto& o = static_cast<const StepSeq&>(other);
    rng_ = o.rng_; index_ = o.index_; direction_ = o.direction_; tick_ = o.tick_;
    active_ = o.active_; clock_edge_ = o.clock_edge_; reset_edge_ = o.reset_edge_;
  }
 private:
  // A live len change or a shorter note list on reload can remove our step.
  void clamp_index(int len) {
    if (index_ < len) return;
    index_ = len - 1;
    tick_ = 0;
    active_ = gate_on(index_);
  }
  // Hold the gate through the clock's low half unless this is the step's
  // last tick and the next step re-articulates. Lookahead is forward-order;
  // slurs are a phrasing feature and phrases run forward.
  bool sustain(int current, int len) const {
    if (tick_ + 1 < step_len(current)) return true;
    const int next = (current + 1) % len;
    return tied(next) && gate_on(next);
  }
  // The `len` input overrides the note count while it is positive.
  int live_len(float in) const {
    const int n = static_cast<int>(steps_.size());
    if (!std::isfinite(in) || in <= 0.0f) return n;
    return static_cast<int>(std::lround(std::clamp(in, 1.0f, static_cast<float>(n))));
  }
  // Moves to the next step in `dir` order and articulates it.
  void advance(int len, bool& trig, bool& eoc) {
    tick_ = 0;
    const int old = index_;
    index_ = next_index(len);
    eoc = old >= 0 && ((dir_ == 0 && index_ == 0) ||
                       (dir_ == 1 && index_ == len - 1));
    const bool on = gate_on(index_);
    // A tied step retriggers nothing; it only carries the note forward
    // (and if the previous step was a rest, it simply starts the note).
    trig = on && !(tied(index_) && active_);
    active_ = on;
  }
  int next_index(int len) {
    switch (dir_) {
      case 0: return (index_ + 1) % len;
      case 1: return index_ < 0 ? len - 1 : (index_ + len - 1) % len;
      case 2: return pingpong(len);
      default: return static_cast<int>(xorshift32(rng_) % static_cast<std::uint32_t>(len));
    }
  }
  int pingpong(int len) {
    if (len == 1) { direction_ = 1; return 0; }
    if (index_ < 0) return 0;
    if (index_ + direction_ >= len || index_ + direction_ < 0) direction_ = -direction_;
    return index_ + direction_;
  }
  int step_len(int i) const {
    return i < static_cast<int>(lens_.size()) ? static_cast<int>(lens_[static_cast<size_t>(i)]) : 1;
  }
  bool gate_on(int i) const {
    return i >= static_cast<int>(gates_.size()) || gates_[static_cast<size_t>(i)] > 0.5;
  }
  bool tied(int i) const {
    return i < static_cast<int>(ties_.size()) && ties_[static_cast<size_t>(i)] > 0.5;
  }
  std::vector<double> steps_;
  std::vector<double> gates_;
  std::vector<double> lens_;
  std::vector<double> ties_;
  std::vector<double> vels_;
  std::uint32_t rng_ = 0x9E3779B9u;
  int dir_ = 0, index_ = -1, direction_ = 1, tick_ = 0;
  bool active_ = false;
  RisingEdge clock_edge_, reset_edge_;
};
std::unique_ptr<Module> make() { return std::make_unique<StepSeq>(); }
}  // namespace
const ModuleInfo stepseq_info{"stepseq", kIns, kCfg, kOuts, &make};
}  // namespace ms::dsp
