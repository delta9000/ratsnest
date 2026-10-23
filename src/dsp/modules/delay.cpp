#include <algorithm>
#include <cmath>
#include <cstddef>
#include <numbers>
#include <vector>
#include "dsp/input_key.h"
#include "dsp/modules.h"

namespace ms::dsp {
namespace {

constexpr InputInfo kIns[] = {{"in", 0.f},        {"time", 0.4f},
                              {"pitch", 0.f},     {"feedback", 0.35f},
                              {"mix", 0.5f},      {"tone", 8000.f},
                              {"slew", 0.1f}};
constexpr OutputInfo kOuts[] = {{"out"}};
constexpr float kMaxSeconds = 10.0f;

class Delay final : public Module {
 public:
  void init(double rate, const Config&) override {
    rate_ = static_cast<float>(rate);
    const auto need = static_cast<std::size_t>(kMaxSeconds * rate) + 4;
    std::size_t size = 1;
    while (size < need) size <<= 1;
    buf_.assign(size, 0.0f);  // control thread: allocation is legal here
    mask_ = size - 1;
    write_ = 0;
    delay_ = -1.0;  // sentinel: snap to the target on the first sample
    lp_ = 0.0f;
    dc_x_ = 0.0f;
    dc_y_ = 0.0f;
  }

  void process(const float* ins, float* outs) override {
    // Control-rate coefficients are memoized while their inputs hold still
    // (dsp/input_key.h): exp2/exp/cos/sin per sample dominated this module.
    if (time_key_.changed({ins[1], ins[2]})) {
      const float secs =
          std::clamp(ins[1], 0.0f, kMaxSeconds) * std::exp2(-ins[2]);
      double target = static_cast<double>(secs) * static_cast<double>(rate_);
      // NaN-proof lower clamp: 0 * inf from a hostile pitch input must not
      // poison delay_ forever. !(x >= 2) catches NaN as well as small values.
      if (!(target >= 2.0)) target = 2.0;
      target_ = std::min(target, static_cast<double>(buf_.size() - 4));
    }
    const double target = target_;
    if (delay_ < 0.0) delay_ = target;
    if (slew_key_.changed({ins[6]})) {
      const float slew_samples = std::max(sanitize(ins[6]), 0.0f) * rate_;
      slew_a_ = slew_samples <= 1.0f
                    ? 1.0
                    : 1.0 - std::exp(-1.0 / static_cast<double>(slew_samples));
    }
    const double a = slew_a_;
    delay_ += (target - delay_) * a;

    // 4-point cubic Hermite read: f interpolates tap di toward tap di+1.
    const auto di = static_cast<std::size_t>(delay_);
    const auto f = static_cast<float>(delay_ - static_cast<double>(di));
    const std::size_t w = write_;
    const float x0 = buf_[(w - (di - 1)) & mask_];
    const float x1 = buf_[(w - di) & mask_];
    const float x2 = buf_[(w - (di + 1)) & mask_];
    const float x3 = buf_[(w - (di + 2)) & mask_];
    const float c1 = 0.5f * (x2 - x0);
    const float c2 = x0 - 2.5f * x1 + 2.0f * x2 - 0.5f * x3;
    const float c3 = 0.5f * (x3 - x0) + 1.5f * (x1 - x2);
    const float wet = ((c3 * f + c2) * f + c1) * f + x1;

    // Feedback path: darken, block DC, saturate, write.
    if (tone_key_.changed({ins[5]})) {
      const float fc = std::clamp(sanitize(ins[5]), 20.0f, 0.45f * rate_);
      tone_g_ = 1.0f - std::exp(-2.0f * std::numbers::pi_v<float> * fc / rate_);
    }
    const float g = tone_g_;
    lp_ += g * (wet - lp_);
    const float rpole =
        1.0f - 2.0f * std::numbers::pi_v<float> * 10.0f / rate_;
    const float dc = lp_ - dc_x_ + rpole * dc_y_;
    dc_x_ = lp_;
    dc_y_ = dc;
    // The offline render path has no FTZ; flush decaying states ourselves so
    // render and play behave identically. dc_x_ (the DC blocker's one-sample
    // input memory) must flush alongside lp_/dc_y_: left unflushed it lags
    // lp_'s flush by a sample and re-seeds `dc` with a stale sub-threshold
    // value every cycle, sustaining a permanent +-1-ULP denormal limit cycle.
    if (std::abs(lp_) < 1e-20f) lp_ = 0.0f;
    if (std::abs(dc_x_) < 1e-20f) dc_x_ = 0.0f;
    if (std::abs(dc_y_) < 1e-20f) dc_y_ = 0.0f;
    const float fb = std::clamp(ins[3], 0.0f, 1.2f);
    const float sat = fb * dc;  // tanh(+-0) is +-0: skip it on a silent loop
    // Never store a non-finite sample: it would replay for up to 10 s.
    buf_[w] = sanitize(ins[0] + (sat == 0.0f ? sat : std::tanh(sat)));
    write_ = (w + 1) & mask_;

    if (mix_key_.changed({ins[4]})) {
      const float m = std::clamp(ins[4], 0.0f, 1.0f);
      const float angle = m * (std::numbers::pi_v<float> / 2.0f);
      dry_gain_ = std::cos(angle);
      wet_gain_ = std::sin(angle);
    }
    outs[0] = ins[0] * dry_gain_ + wet * wet_gain_;
  }

  void copy_state_from(const Module& other) override {
    const auto& o = static_cast<const Delay&>(other);
    // RT-called: bounded copy, no allocation. Sizes match in practice (same
    // rate); min() is defense if they ever don't.
    const std::size_t n = std::min(buf_.size(), o.buf_.size());
    std::copy_n(o.buf_.begin(), n, buf_.begin());
    write_ = o.write_ & mask_;
    delay_ = std::min(o.delay_, static_cast<double>(buf_.size() - 4));
    lp_ = o.lp_;
    dc_x_ = o.dc_x_;
    dc_y_ = o.dc_y_;
  }

 private:
  float rate_ = 48000.0f;
  std::vector<float> buf_;
  std::size_t mask_ = 0;
  std::size_t write_ = 0;
  double delay_ = -1.0;
  float lp_ = 0.0f;
  float dc_x_ = 0.0f;
  float dc_y_ = 0.0f;
  InputKey<2> time_key_;
  InputKey<1> slew_key_, tone_key_, mix_key_;
  double target_ = 2.0, slew_a_ = 1.0;
  float tone_g_ = 0.0f, dry_gain_ = 1.0f, wet_gain_ = 0.0f;
};

std::unique_ptr<Module> make() { return std::make_unique<Delay>(); }
}  // namespace

const ModuleInfo delay_info{"delay", kIns, {}, kOuts, &make};
}  // namespace ms::dsp
