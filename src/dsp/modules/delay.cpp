#include <algorithm>
#include <cmath>
#include <numbers>
#include "dsp/delay_line.h"
#include "dsp/input_key.h"
#include "dsp/modules.h"

namespace ms::dsp {
namespace {

constexpr InputInfo kIns[] = {{"in", 0.f},        in_secs("time", 0.4f),
                              {"pitch", 0.f},     {"feedback", 0.35f},
                              {"mix", 0.5f},      {"tone", 8000.f},
                              in_secs("slew", 0.1f)};
constexpr OutputInfo kOuts[] = {{"out"}};

class Delay final : public Module {
 public:
  void init(double rate, const Config&) override {
    rate_ = static_cast<float>(rate);
    line_.init(rate);  // control thread: allocation is legal here
  }

  void process(const float* ins, float* outs) override {
    // Control-rate coefficients are memoized while their inputs hold still
    // (dsp/input_key.h): exp2/exp/cos/sin per sample dominated this module.
    if (time_key_.changed({ins[1], ins[2]}))
      target_ = echo_samples(ins[1], ins[2], rate_, line_.max_samples());
    if (slew_key_.changed({ins[6]})) slew_a_ = echo_slew(ins[6], rate_);
    const float wet = line_.read(target_, slew_a_);
    if (tone_key_.changed({ins[5]})) tone_g_ = echo_tone(ins[5], rate_);
    line_.write(ins[0], wet, tone_g_, std::clamp(ins[3], 0.0f, 1.2f));

    if (mix_key_.changed({ins[4]})) {
      const float m = std::clamp(ins[4], 0.0f, 1.0f);
      const float angle = m * (std::numbers::pi_v<float> / 2.0f);
      dry_gain_ = std::cos(angle);
      wet_gain_ = std::sin(angle);
    }
    outs[0] = ins[0] * dry_gain_ + wet * wet_gain_;
  }

  void copy_state_from(const Module& other) override {
    line_.copy_from(static_cast<const Delay&>(other).line_);
  }

 private:
  float rate_ = 48000.0f;
  EchoLine line_;
  InputKey<2> time_key_;
  InputKey<1> slew_key_, tone_key_, mix_key_;
  double target_ = 2.0, slew_a_ = 1.0;
  float tone_g_ = 0.0f, dry_gain_ = 1.0f, wet_gain_ = 0.0f;
};

std::unique_ptr<Module> make() { return std::make_unique<Delay>(); }
}  // namespace

const ModuleInfo delay_info{"delay", kIns, {}, kOuts, &make};
}  // namespace ms::dsp
