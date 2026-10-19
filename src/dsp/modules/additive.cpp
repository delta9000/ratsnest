#include <algorithm>
#include <array>
#include <cmath>
#include <numbers>
#include "dsp/modules.h"

namespace ms::dsp {
namespace {
constexpr size_t kMaxPartials = 128;
constexpr InputInfo kIns[] = {{"freq", 220.f}, {"v_oct", 0.f}, {"gain", 1.f},
                              {"tilt", 0.f}, {"phase", 0.f}, {"sync", 0.f}};
constexpr ConfigInfo kCfg[] = {{"levels", nullptr, ConfigKind::Numbers},
                               {"ratios", nullptr, ConfigKind::Numbers},
                               {"phases", nullptr, ConfigKind::Numbers}};
constexpr OutputInfo kOuts[] = {{"out"}};

std::string validate(const Config& cfg) {
  size_t count = 1;
  for (const auto& [name, value] : cfg) if (name == "levels") {
    count = value.arr.size();
    if (count == 0 || count > kMaxPartials) return "'levels' needs 1..128 partial amplitudes";
  }
  for (const auto& [name, value] : cfg) {
    if (value.arr.size() != count) return "'" + name + "' must have the same length as levels";
    for (double x : value.arr) {
      if (!std::isfinite(x)) return "'" + name + "' needs finite values";
      if (name == "ratios" && (x <= 0 || x > 1e6)) return "'ratios' entries must be > 0 and <= 1000000";
      if (name == "levels" && std::abs(x) > 1) return "'levels' entries must be in -1..1; use gain for output level";
    }
  }
  return {};
}

double finite(float x, double fallback = 0) { return std::isfinite(x) ? x : fallback; }

class Additive final : public Module {
 public:
  void init(double rate, const Config& cfg) override {
    rate_ = rate;
    const auto& levels = cfg_arr(cfg, "levels");
    const auto& ratios = cfg_arr(cfg, "ratios");
    const auto& phases = cfg_arr(cfg, "phases");
    count_ = std::clamp(levels.size(), size_t{1}, kMaxPartials);
    for (size_t i = 0; i < count_; ++i) {
      levels_[i] = i < levels.size() ? levels[i] : 1.0;
      ratios_[i] = i < ratios.size() ? ratios[i] : static_cast<double>(i + 1);
      offsets_[i] = i < phases.size() ? phases[i] - std::floor(phases[i]) : 0.0;
      octaves_[i] = std::log2(ratios_[i]);
    }
    phases_.fill(0);
    prev_sync_ = false;
  }

  void process(const float* ins, float* outs) override {
    const bool sync = ins[5] > 0.5f;
    if (sync && !prev_sync_) phases_.fill(0);
    prev_sync_ = sync;
    const double f0 = std::clamp(finite(ins[0]), 0.0, rate_) *
                      std::exp2(std::clamp(finite(ins[1]), -24.0, 24.0));
    const double tilt = std::clamp(finite(ins[3]), -48.0, 48.0);
    double out = 0;
    for (size_t i = 0; i < count_; ++i) {
      const double f = f0 * ratios_[i];
      phases_[i] += f / rate_;
      phases_[i] -= std::floor(phases_[i]);
      // Fade the last tenth of the representable band, then remove the
      // partial. Clamping its frequency would invent a different spectrum.
      const double band = std::clamp((0.5 * rate_ - f) / (0.05 * rate_), 0.0, 1.0);
      if (band == 0) continue;
      double phase = phases_[i] + offsets_[i] + finite(ins[4]);
      phase -= std::floor(phase);
      const double level = levels_[i] * std::pow(10.0,
          std::clamp(tilt * octaves_[i], -120.0, 120.0) / 20.0);
      out += band * level * std::sin(2.0 * std::numbers::pi * phase);
    }
    outs[0] = static_cast<float>(out * std::clamp(finite(ins[2]), -8.0, 8.0));
  }

  void copy_state_from(const Module& other) override {
    const auto& o = static_cast<const Additive&>(other);
    // Partial identity is its array position. New positions start at zero.
    std::copy_n(o.phases_.begin(), std::min(count_, o.count_), phases_.begin());
    prev_sync_ = o.prev_sync_;
  }

 private:
  double rate_ = 48000;
  size_t count_ = 1;
  std::array<double, kMaxPartials> levels_{}, ratios_{}, offsets_{}, octaves_{}, phases_{};
  bool prev_sync_ = false;
};
std::unique_ptr<Module> make() { return std::make_unique<Additive>(); }
}  // namespace
const ModuleInfo additive_info{"additive", kIns, kCfg, kOuts, &make, false, false, &validate};
}  // namespace ms::dsp
