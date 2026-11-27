#include <algorithm>
#include <cmath>
#include <numbers>
#include "dsp/input_key.h"
#include "dsp/mastering.h"
#include "dsp/modules.h"
#include "dsp/oversample.h"

// Stereo bus saturation: a level-compensated, optionally biased tanh at 4x
// (the clipper's oversampler). Concept-generic; see docs/attribution.md.

namespace ms::dsp {
namespace {

constexpr InputInfo kIns[] = {{"in_l", 0.0f},
                              {"in_r", 0.0f},
                              {"drive", 0.0f},  // dB
                              {"bias", 0.0f},   // -1..1: even harmonics
                              {"mix", 1.0f}};
constexpr OutputInfo kOuts[] = {{"out_l"}, {"out_r"}};
constexpr int kLatency = 1;  // the 4x polyphase pair's peak delay, as in clipper

class Saturate final : public Module {
 public:
  void init(double rate, const Config&) override {
    // 5 Hz one-pole high-pass: bias makes DC that must not reach the limiter.
    dc_r_ = 1.0f - 2.0f * std::numbers::pi_v<float> * 5.0f / static_cast<float>(rate);
    for (Chan& c : ch_) c = {};
  }

  void process(const float* ins, float* outs) override {
    const float drive = sanitize_clamp(ins[2], -12.0f, 36.0f, 0.0f);
    const float bias = sanitize_clamp(ins[3], -1.0f, 1.0f, 0.0f);
    const float mix = sanitize_clamp(ins[4], 0.0f, 1.0f, 1.0f);
    if (key_.changed({drive, bias})) {
      k_ = db_to_linear(drive);
      c_ = 0.5f * bias;
      t_ = std::tanh(c_);
      norm_ = 1.0f / (k_ * (1.0f - t_ * t_));  // unity slope at zero
    }
    for (int c = 0; c < 2; ++c) {
      Chan& s = ch_[c];
      const float x = sanitize(ins[c], 0.0f);
      s.dry[s.idx] = x;
      const float dry = s.dry[(s.idx + 8 - kLatency) % 8];
      s.idx = (s.idx + 1) % 8;
      float up[4], ys[4];
      s.up.upsample(x, up);
      for (int i = 0; i < 4; ++i) ys[i] = (std::tanh(k_ * up[i] + c_) - t_) * norm_;
      const float wet = s.down.downsample(ys);
      s.dc_y = flush_denormal(wet - s.dc_x + dc_r_ * s.dc_y);
      s.dc_x = wet;
      outs[c] = (1.0f - mix) * dry + mix * s.dc_y;
    }
  }

  void copy_state_from(const Module& other) override {
    const auto& o = static_cast<const Saturate&>(other);
    ch_[0] = o.ch_[0];
    ch_[1] = o.ch_[1];
  }

 private:
  struct Chan {
    Oversampler4x up;
    Decimator4x down;
    float dry[8] = {};
    int idx = 0;
    float dc_x = 0.0f, dc_y = 0.0f;
  };
  Chan ch_[2];
  float dc_r_ = 0.9993f;
  InputKey<2> key_;
  float k_ = 1.0f, c_ = 0.0f, t_ = 0.0f, norm_ = 1.0f;
};

std::unique_ptr<Module> make() { return std::make_unique<Saturate>(); }
}  // namespace

const ModuleInfo saturate_info{"saturate", kIns, {}, kOuts, &make};
}  // namespace ms::dsp
