#include <algorithm>
#include <cmath>
#include <numbers>
#include "dsp/delay_line.h"
#include "dsp/input_key.h"
#include "dsp/modules.h"

// Stereo echo: two `delay` lines (dsp/delay_line.h) side by side, or
// cross-fed for ping-pong. Concept-generic; see docs/attribution.md.

namespace ms::dsp {
namespace {

constexpr InputInfo kIns[] = {{"in_l", 0.f},     {"in_r", 0.f},  {"time", 0.375f},
                              {"feedback", 0.4f}, {"mix", 0.5f},  {"tone", 6000.f},
                              {"spread", 0.f},    {"slew", 0.1f}};
constexpr const char* kModes[] = {"ping", "stereo", nullptr};
constexpr ConfigInfo kCfg[] = {{"mode", kModes}};
constexpr OutputInfo kOuts[] = {{"out_l"}, {"out_r"}};

class Echo final : public Module {
 public:
  void init(double rate, const Config& cfg) override {
    rate_ = static_cast<float>(rate);
    ping_ = cfg_enum(cfg, "mode", kModes) == 0;
    for (EchoLine& l : line_) l.init(rate);  // control thread: allocation is legal here
  }

  void process(const float* ins, float* outs) override {
    if (time_key_.changed({ins[2], ins[6]})) {
      // `spread` stretches the right side's time (0..1 -> x1..x2): a
      // Haas-like offset on stereo, an uneven bounce on ping.
      const float spread = std::clamp(sanitize(ins[6]), 0.0f, 1.0f);
      const double max = line_[0].max_samples();
      target_[0] = echo_samples(ins[2], 0.0f, rate_, max);
      target_[1] = echo_samples(ins[2] * (1.0f + spread), 0.0f, rate_, max);
    }
    if (slew_key_.changed({ins[7]})) slew_a_ = echo_slew(ins[7], rate_);
    if (tone_key_.changed({ins[5]})) tone_g_ = echo_tone(ins[5], rate_);
    const float wet[2] = {line_[0].read(target_[0], slew_a_), line_[1].read(target_[1], slew_a_)};
    const float fb = std::clamp(ins[3], 0.0f, 1.2f);
    if (ping_) {
      // The mono sum enters on the left; each side's echo feeds the other,
      // so repeats alternate L, R, L... at `time`.
      line_[0].write(0.5f * (ins[0] + ins[1]), wet[1], tone_g_, fb);
      line_[1].write(0.0f, wet[0], tone_g_, fb);
    } else {
      line_[0].write(ins[0], wet[0], tone_g_, fb);
      line_[1].write(ins[1], wet[1], tone_g_, fb);
    }
    if (mix_key_.changed({ins[4]})) {
      const float angle = std::clamp(ins[4], 0.0f, 1.0f) * (std::numbers::pi_v<float> / 2.0f);
      dry_ = std::cos(angle);
      wet_ = std::sin(angle);
    }
    outs[0] = ins[0] * dry_ + wet[0] * wet_;
    outs[1] = ins[1] * dry_ + wet[1] * wet_;
  }

  void copy_state_from(const Module& other) override {
    const auto& o = static_cast<const Echo&>(other);
    line_[0].copy_from(o.line_[0]);
    line_[1].copy_from(o.line_[1]);
  }

 private:
  float rate_ = 48000.0f;
  bool ping_ = true;
  EchoLine line_[2];
  InputKey<2> time_key_;
  InputKey<1> slew_key_, tone_key_, mix_key_;
  double target_[2] = {2.0, 2.0}, slew_a_ = 1.0;
  float tone_g_ = 0.0f, dry_ = 1.0f, wet_ = 0.0f;
};

std::unique_ptr<Module> make() { return std::make_unique<Echo>(); }
}  // namespace

const ModuleInfo echo_info{"echo", kIns, kCfg, kOuts, &make};
}  // namespace ms::dsp
