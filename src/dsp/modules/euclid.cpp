#include <algorithm>
#include <cmath>
#include "dsp/modules.h"

namespace ms::dsp {
namespace {

constexpr InputInfo kIns[] = {{"clock", 0.f}, {"reset", 0.f},
                              {"steps", 16.f}, {"hits", 4.f},
                              {"rot", 0.f}};
constexpr OutputInfo kOuts[] = {{"trig"}, {"gate"}};

int rounded_clamp(float value, int lo, int hi, int fallback) {
  if (!std::isfinite(value)) return fallback;
  return static_cast<int>(
      std::lround(std::clamp(value, static_cast<float>(lo),
                             static_cast<float>(hi))));
}

class Euclid final : public Module {
 public:
  void process(const float* ins, float* outs) override {
    const bool reset = ins[1] > 0.5f && prev_reset_ <= 0.5f;
    const bool rising = ins[0] > 0.5f && prev_clock_ <= 0.5f;
    prev_reset_ = ins[1];
    prev_clock_ = ins[0];

    if (reset) {
      step_ = -1;
      hit_ = false;
    }

    bool trigger = false;
    if (rising) {
      const int steps = rounded_clamp(ins[2], 1, 64, 16);
      const int hits = rounded_clamp(ins[3], 0, steps, 4);
      step_ = (step_ + 1) % steps;

      double rot_value = std::isfinite(ins[4]) ? static_cast<double>(ins[4]) : 0.0;
      rot_value = std::fmod(rot_value, static_cast<double>(steps));
      int rotation = static_cast<int>(std::lround(rot_value)) % steps;
      if (rotation < 0) rotation += steps;

      hit_ = hits > 0 && ((step_ + rotation) * hits) % steps < hits;
      trigger = hit_;
    }

    outs[0] = trigger ? 1.0f : 0.0f;
    outs[1] = hit_ && ins[0] > 0.5f ? 1.0f : 0.0f;
  }

  void copy_state_from(const Module& other) override {
    const auto& o = static_cast<const Euclid&>(other);
    step_ = o.step_;
    hit_ = o.hit_;
    prev_clock_ = o.prev_clock_;
    prev_reset_ = o.prev_reset_;
  }

 private:
  int step_ = -1;
  bool hit_ = false;
  float prev_clock_ = 0.0f;
  float prev_reset_ = 0.0f;
};

std::unique_ptr<Module> make() { return std::make_unique<Euclid>(); }
}  // namespace

const ModuleInfo euclid_info{"euclid", kIns, {}, kOuts, &make};
}  // namespace ms::dsp
