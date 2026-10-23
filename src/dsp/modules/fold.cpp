#include <algorithm>
#include <cmath>
#include "dsp/modules.h"
#include "dsp/nonlinear_oversample.h"

namespace ms::dsp {
namespace {

constexpr InputInfo kIns[] = {{"in", 0.f}, {"fold", 1.f}, {"sym", 0.f}};
constexpr const char* kQualities[] = {"raw", "clean", nullptr};
constexpr ConfigInfo kCfg[] = {{"quality", kQualities}};
constexpr OutputInfo kOuts[] = {{"out"}};

float fold_curve(float x) {
  float t = (x + 1.0f) * 0.25f;
  t -= std::floor(t);
  return 1.0f - 4.0f * std::abs(t - 0.5f);
}

double fold_antiderivative(double x) {
  const double k = std::floor((x + 1.0) * 0.5);
  const bool odd = std::fmod(std::abs(k), 2.0) >= 1.0;
  const double sign = odd ? -1.0 : 1.0;
  const double local = x - 2.0 * k;
  return sign * local * local * 0.5 + (odd ? 1.0 : 0.0);
}

class FoldAdaa {
 public:
  void reset() { previous_ = 0.0f; }

  float process(float x) {
    const double current = static_cast<double>(x);
    const double previous = static_cast<double>(previous_);
    const double dx = current - previous;
    float output;
    if (std::abs(dx) < 1.0e-5) {
      output = fold_curve(static_cast<float>(0.5 * (current + previous)));
    } else {
      output = static_cast<float>(
          (fold_antiderivative(current) - fold_antiderivative(previous)) / dx);
    }
    previous_ = x;
    return output;
  }

 private:
  float previous_ = 0.0f;
};

class Fold final : public Module {
 public:
  void init(double, const Config& cfg) override {
    clean_ = cfg_str(cfg, "quality", "raw") == "clean";
    clean_oversampler_.reset();
    clean_shape_.reset();
  }

  void process(const float* ins, float* outs) override {
    if (!clean_) {
      const float x = ins[0] * std::max(ins[1], 0.0f) + ins[2];
      float t = (x + 1.0f) * 0.25f;
      t -= std::floor(t);
      outs[0] = 1.0f - 4.0f * std::abs(t - 0.5f);
      return;
    }

    const float input = std::isfinite(ins[0]) ? ins[0] : 0.0f;
    const float fold = std::isfinite(ins[1]) ? std::max(ins[1], 0.0f) : 0.0f;
    const float sym = std::isfinite(ins[2]) ? ins[2] : 0.0f;
    const float combined = input * fold + sym;
    const float x = std::isfinite(combined)
        ? std::clamp(combined, -1.0e6f, 1.0e6f)
        : 0.0f;
    outs[0] = clean_oversampler_.process_phase<1>(x, [this](float high_rate) {
      return clean_shape_.process(high_rate);
    });
  }

  void copy_state_from(const Module& other) override {
    const auto& o = static_cast<const Fold&>(other);
    if (clean_ && o.clean_) {
      clean_oversampler_ = o.clean_oversampler_;
      clean_shape_ = o.clean_shape_;
    }
  }

 private:
  bool clean_ = false;
  NonlinearOversampler<8> clean_oversampler_{};
  FoldAdaa clean_shape_{};
};

std::unique_ptr<Module> make() { return std::make_unique<Fold>(); }
}  // namespace

const ModuleInfo fold_info{"fold", kIns, kCfg, kOuts, &make};
}  // namespace ms::dsp
