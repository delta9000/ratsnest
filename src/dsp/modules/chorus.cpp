// Stereo multi-voice chorus: N short modulated delay taps with decorrelated
// LFO phases, spread across the stereo field and summed with the dry signal.
// Textbook modulated-delay DSP, written locally; see docs/attribution.md.
#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <numbers>
#include <string>
#include <vector>
#include "dsp/modules.h"

namespace ms::dsp {
namespace {

constexpr int kMaxVoices = 4;
constexpr float kTwoPi = 2.0f * std::numbers::pi_v<float>;

constexpr InputInfo kIns[] = {
    {"in_l", 0.f}, {"in_r", 0.f}, {"rate", 0.5f}, {"depth", 0.5f}, {"mix", 0.5f}};
constexpr const char* kShapes[] = {"tri", "sine", nullptr};
constexpr ConfigInfo kCfg[] = {
    {"voices", nullptr}, {"delay", nullptr}, {"swing", nullptr}, {"shape", kShapes}};
constexpr OutputInfo kOuts[] = {{"out_l"}, {"out_r"}};

float clampn(float x, float lo, float hi) {
  if (!(x >= lo)) return lo;
  return x > hi ? hi : x;
}
float lfo_shape(int shape, float ph) {  // ph in [0,1) -> [-1,1]
  if (shape == 1) return std::sin(kTwoPi * ph);
  return 1.0f - 4.0f * std::fabs(ph - 0.5f);  // triangle
}

class Chorus final : public Module {
 public:
  void init(double rate, const Config& cfg) override {
    rate_ = static_cast<float>(rate);
    voices_ = static_cast<int>(clampn(static_cast<float>(cfg_num(cfg, "voices", 3.0)),
                                      1.0f, static_cast<float>(kMaxVoices)) + 0.5f);
    delay_ms_ = clampn(static_cast<float>(cfg_num(cfg, "delay", 12.0)), 1.0f, 40.0f);
    swing_ms_ = clampn(static_cast<float>(cfg_num(cfg, "swing", 6.0)), 0.0f, 20.0f);
    shape_ = cfg_str(cfg, "shape", "tri") == "sine" ? 1 : 0;

    const auto need =
        static_cast<std::size_t>((delay_ms_ + swing_ms_) * 1e-3f * rate_) + 4;
    std::size_t size = 1;
    while (size < need) size <<= 1;
    bufL_.assign(size, 0.0f);
    bufR_.assign(size, 0.0f);
    mask_ = size - 1;
    write_ = 0;
    phase_ = 0.0f;
  }

  void process(const float* ins, float* outs) override {
    const float in_l = ins[0], in_r = ins[1];
    const float rate = clampn(ins[2], 0.0f, 100.0f);
    const float depth = clampn(ins[3], 0.0f, 1.0f);
    const float mix = clampn(ins[4], 0.0f, 1.0f);

    // Non-finite input must not reach the delay lines: it would replay from
    // every tap for a full buffer length after the input recovers.
    bufL_[write_] = sanitize(in_l);
    bufR_[write_] = sanitize(in_r);

    const float base = delay_ms_ * 1e-3f * rate_;   // samples
    const float swing = swing_ms_ * 1e-3f * rate_;  // samples
    float wetL = 0.0f, wetR = 0.0f;
    for (int i = 0; i < voices_; ++i) {
      const float off = static_cast<float>(i) / static_cast<float>(voices_);
      const float phL = phase_ + off;
      const float phR = phase_ + off + 0.5f;  // R half-cycle out of phase
      const float dL = base + depth * swing * lfo_shape(shape_, phL - std::floor(phL));
      const float dR = base + depth * swing * lfo_shape(shape_, phR - std::floor(phR));
      wetL += read(bufL_, dL);
      wetR += read(bufR_, dR);
    }
    const float norm = 1.0f / static_cast<float>(voices_);
    wetL *= norm;
    wetR *= norm;

    phase_ += rate / rate_;
    phase_ -= std::floor(phase_);

    outs[0] = (1.0f - mix) * in_l + mix * wetL;
    outs[1] = (1.0f - mix) * in_r + mix * wetR;
    write_ = (write_ + 1) & mask_;
  }

  void copy_state_from(const Module& other) override {
    const auto& o = static_cast<const Chorus&>(other);
    const std::size_t n = std::min(bufL_.size(), o.bufL_.size());
    std::copy_n(o.bufL_.begin(), n, bufL_.begin());
    std::copy_n(o.bufR_.begin(), n, bufR_.begin());
    write_ = o.write_ & mask_;
    phase_ = o.phase_;
  }

 private:
  float read(const std::vector<float>& buf, float d) const {
    const float dc = d < 1.0f ? 1.0f
                              : (d > static_cast<float>(buf.size() - 2)
                                     ? static_cast<float>(buf.size() - 2)
                                     : d);
    const auto di = static_cast<std::size_t>(dc);
    const float f = dc - static_cast<float>(di);
    const float x1 = buf[(write_ - di) & mask_];
    const float x2 = buf[(write_ - (di + 1)) & mask_];
    return x1 + f * (x2 - x1);
  }

  float rate_ = 48000.0f;
  int voices_ = 3;
  int shape_ = 0;
  float delay_ms_ = 12.0f, swing_ms_ = 6.0f;
  std::vector<float> bufL_, bufR_;
  std::size_t mask_ = 0;
  std::size_t write_ = 0;
  float phase_ = 0.0f;
};

std::unique_ptr<Module> make() { return std::make_unique<Chorus>(); }
}  // namespace

const ModuleInfo chorus_info{"chorus", kIns, kCfg, kOuts, &make};
}  // namespace ms::dsp
