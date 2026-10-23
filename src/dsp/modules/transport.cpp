#include "dsp/mastering.h"
#include "dsp/modules.h"
#include "dsp/util.h"
#include <algorithm>
#include <cmath>
#include <cstdint>

namespace ms::dsp {
namespace {
constexpr InputInfo kIns[] = {
    {"bpm", 120.f}, {"run", 1.f, false}, {"reset", 0.f, false}, {"swing", 0.f}};
constexpr ConfigInfo kCfg[] = {
    {"beats", nullptr}, {"bars", nullptr}, {"division", nullptr}};
constexpr OutputInfo kOuts[] = {{"clock"},     {"gate"},         {"beat"},
                                {"bar"},       {"phrase"},       {"beat_phase"},
                                {"bar_phase"}, {"phrase_phase"}, {"running"}};
std::string validate(const Config &cfg) {
  for (const auto &[key, v] : cfg) {
    const double max = key == "bars" ? 256 : 32;
    if (v.num < 1 || v.num > max || std::floor(v.num) != v.num)
      return "'" + key + "' must be an integer in 1.." +
             std::to_string(static_cast<int>(max));
    if (key == "division" && v.num != 1 && static_cast<int>(v.num) % 2)
      return "division must be 1 or even (swing operates on pairs)";
  }
  return {};
}
class Transport final : public Module {
public:
  void init(double rate, const Config &cfg) override {
    rate_ = rate;
    beats_ = static_cast<int>(std::clamp(cfg_num(cfg, "beats", 4), 1.0, 32.0));
    bars_ = static_cast<int>(std::clamp(cfg_num(cfg, "bars", 4), 1.0, 256.0));
    division_ =
        static_cast<int>(std::clamp(cfg_num(cfg, "division", 4), 1.0, 32.0));
  }
  void process(const float *in, float *out) override {
    const bool reset = in[2] > .5f;
    if (reset && !reset_)
      restart();
    reset_ = reset;
    const double bpm = sanitize_clamp(in[0], 0, 1000);
    const bool run = in[1] > .5f && bpm > 0;
    if (boundary_)
      swing_ = sanitize_clamp(in[3], 0, .75f);
    double local = 0;
    const int step = swung_step(local);
    const bool beat = run && boundary_;
    const auto beats = static_cast<unsigned>(beats_);
    const auto length = static_cast<unsigned>(beats_ * bars_);
    out[0] = as_gate(run && (boundary_ || step != step_));
    out[1] = as_gate(run && local < .5);
    out[2] = as_gate(beat);
    out[3] = as_gate(beat && beat_ % beats == 0);
    out[4] = as_gate(beat && beat_ % length == 0);
    out[5] = static_cast<float>(phase_);
    out[6] = static_cast<float>(
        (static_cast<double>(beat_ % beats) + phase_) / beats_);
    out[7] = static_cast<float>((static_cast<double>(beat_ % length) + phase_) /
                                length);
    out[8] = as_gate(run);
    if (run)
      tick(step, bpm);
  }
  void copy_state_from(const Module &other) override {
    const auto &o = static_cast<const Transport &>(other);
    phase_ = o.phase_;
    beat_ = o.beat_;
    boundary_ = o.boundary_;
    step_ = o.step_;
    reset_ = o.reset_;
    swing_ = o.swing_;
  }

private:
  void restart() {
    phase_ = 0;
    beat_ = 0;
    boundary_ = true;
    step_ = -1;
  }
  // Index of the current `division` step within the beat, with every second
  // step of a pair delayed by swing; `local` is the phase within that step.
  int swung_step(double &local) const {
    const double x = phase_ * division_;
    const int pair = static_cast<int>(x / 2);
    const double p = x - 2 * pair;
    const double split = division_ == 1 ? 1 : 1 + swing_;
    const bool second = p >= split;
    local = second ? (p - split) / (2 - split) : p / split;
    return 2 * pair + (second ? 1 : 0);
  }
  void tick(int step, double bpm) {
    step_ = step;
    boundary_ = false;
    phase_ += bpm / (60.0 * rate_);
    if (phase_ >= 1.0 - 1e-12) {
      phase_ = std::max(0.0, phase_ - 1);
      ++beat_;
      boundary_ = true;
    }
  }

  double rate_ = 48000, phase_ = 0;
  std::uint64_t beat_ = 0;
  int beats_ = 4, bars_ = 4, division_ = 4, step_ = -1;
  float swing_ = 0;
  bool boundary_ = true, reset_ = false;
};
std::unique_ptr<Module> make() { return std::make_unique<Transport>(); }
} // namespace
const ModuleInfo transport_info{"transport", kIns,  kCfg,  kOuts,
                                &make,       false, false, &validate};
} // namespace ms::dsp
