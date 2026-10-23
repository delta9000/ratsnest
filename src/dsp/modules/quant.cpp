#include "dsp/mastering.h"
#include "dsp/modules.h"
#include <algorithm>
#include <array>
#include <cmath>

namespace ms::dsp {
namespace {
constexpr InputInfo kIns[] = {
    {"in", 0.f},           {"root", 0.f},           {"transpose", 0.f},
    {"clock", 0.f, false}, {"mask", 4095.f, false}, {"degree", 0.f, false}};
constexpr const char *kModes[] = {"continuous", "clock", nullptr};
constexpr ConfigInfo kCfg[] = {{"scale", nullptr, ConfigKind::Numbers},
                               {"mode", kModes}};
constexpr OutputInfo kOuts[] = {{"out"}, {"changed"}};
std::string validate(const Config &cfg) {
  for (double x : cfg_arr(cfg, "scale"))
    if (!std::isfinite(x) || std::abs(x) > 1e6 || std::floor(x) != x)
      return "scale entries must be integer semitones within +/-1000000";
  return {};
}
class Quant final : public Module {
public:
  void init(double, const Config &cfg) override {
    scale_.fill(false);
    const auto &arr = cfg_arr(cfg, "scale");
    if (arr.empty())
      scale_.fill(true);
    for (double d : arr)
      if (std::isfinite(d)) {
        int semitone = static_cast<int>(std::fmod(std::round(d), 12));
        if (semitone < 0)
          semitone += 12;
        scale_[static_cast<size_t>(semitone)] = true;
      }
    clocked_ = cfg_str(cfg, "mode", "continuous") == "clock";
  }
  void process(const float *in, float *out) override {
    const bool clock = in[3] > .5f, edge = clock && !clock_;
    clock_ = clock;
    out[1] = 0;
    if (!clocked_ || edge) {
      const int mask =
          static_cast<int>(std::round(sanitize_clamp(in[4], 0, 4095, 4095)));
      std::array<int, 12> degrees{};
      int count = 0;
      for (int i = 0; i < 12; ++i)
        if (scale_[static_cast<size_t>(i)] && (mask & (1 << i)))
          degrees[static_cast<size_t>(count++)] = i;
      // An empty mask holds the previous note, never fabricates an out-of-scale
      // note.
      if (count > 0) {
        const double root = sanitize_clamp(in[1], -1200, 1200);
        const double semis = std::clamp(
            (static_cast<double>(sanitize(in[0])) + sanitize(in[2])) * 12 -
                root,
            -12000.0, 12000.0);
        const int octave = static_cast<int>(std::floor(semis / 12));
        double distance = 1e9;
        int best = 0;
        for (int o = octave - 1; o <= octave + 1; ++o)
          for (int d = 0; d < count; ++d) {
            const double dist =
                std::abs(o * 12 + degrees[static_cast<size_t>(d)] - semis);
            if (dist < distance) {
              distance = dist;
              best = o * count + d;
            }
          }
        best += static_cast<int>(std::round(sanitize_clamp(in[5], -128, 128)));
        const int o =
            static_cast<int>(std::floor(static_cast<double>(best) / count));
        const int d = best - o * count;
        const float pitch = static_cast<float>(
            (o * 12 + degrees[static_cast<size_t>(d)] + root) / 12);
        out[1] = !first_ && pitch != held_ ? 1.f : 0.f;
        held_ = pitch;
        first_ = false;
      }
    }
    out[0] = held_;
  }
  void copy_state_from(const Module &other) override {
    const auto &o = static_cast<const Quant &>(other);
    first_ = o.first_;
    held_ = o.held_;
    clock_ = o.clock_;
  }

private:
  std::array<bool, 12> scale_{};
  float held_ = 0;
  bool clocked_ = false, clock_ = false, first_ = true;
};
std::unique_ptr<Module> make() { return std::make_unique<Quant>(); }
} // namespace
const ModuleInfo quant_info{"quant", kIns,  kCfg,  kOuts,
                            &make,   false, false, &validate};
} // namespace ms::dsp
