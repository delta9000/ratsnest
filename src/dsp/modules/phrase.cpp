#include "dsp/mastering.h"
#include "dsp/modules.h"
#include <algorithm>
#include <array>
#include <cmath>

namespace ms::dsp {
namespace {
constexpr InputInfo kIns[] = {
    {"in", 0.f},           {"clock", 0.f, false},   {"freeze", 0.f, false},
    {"reset", 0.f, false}, {"length", 16.f, false}, {"rotate", 0.f, false},
    {"transpose", 0.f}};
constexpr ConfigInfo kCfg[] = {{"values", nullptr, ConfigKind::Numbers}};
constexpr OutputInfo kOuts[] = {{"out"},  {"tap1"}, {"tap2"},    {"tap3"},
                                {"trig"}, {"eoc"},  {"position"}};
std::string validate(const Config &cfg) {
  const auto &v = cfg_arr(cfg, "values");
  if (v.size() > 256)
    return "values supports at most 256 steps";
  for (double x : v)
    if (!std::isfinite(x) || std::abs(x) > 1e6)
      return "values must be finite and within +/-1000000";
  return {};
}
class Phrase final : public Module {
public:
  void init(double, const Config &cfg) override {
    const auto &v = cfg_arr(cfg, "values");
    for (size_t i = 0; i < std::min(v.size(), memory_.size()); ++i)
      memory_[i] = static_cast<float>(v[i]);
  }
  void process(const float *in, float *out) override {
    const bool clock = in[1] > .5f, reset = in[3] > .5f;
    const bool edge = clock && !clock_;
    if (reset && !reset_)
      position_ = -1;
    clock_ = clock;
    reset_ = reset;
    const int length =
        static_cast<int>(std::round(sanitize_clamp(in[4], 1, 256, 16)));
    bool eoc = false;
    if (edge) {
      const int old = position_;
      position_ = (position_ + 1) % length;
      eoc = old >= 0 && position_ == 0;
      if (in[2] <= .5f)
        memory_[static_cast<size_t>(position_)] = sanitize(in[0]);
    }
    const int rotate =
        static_cast<int>(std::round(sanitize_clamp(in[5], -256, 256)));
    for (int tap = 0; tap < 4; ++tap) {
      const int index =
          ((std::max(position_, 0) + rotate - tap) % length + length) % length;
      out[tap] = memory_[static_cast<size_t>(index)] + sanitize(in[6]);
    }
    out[4] = edge ? 1.f : 0.f;
    out[5] = eoc ? 1.f : 0.f;
    out[6] = static_cast<float>(std::max(position_, 0));
  }
  void copy_state_from(const Module &other) override {
    const auto &o = static_cast<const Phrase &>(other);
    memory_ = o.memory_;
    position_ = o.position_;
    clock_ = o.clock_;
    reset_ = o.reset_;
  }

private:
  std::array<float, 256> memory_{};
  int position_ = -1;
  bool clock_ = false, reset_ = false;
};
std::unique_ptr<Module> make() { return std::make_unique<Phrase>(); }
} // namespace
const ModuleInfo phrase_info{"phrase", kIns,  kCfg,  kOuts,
                             &make,    false, false, &validate};
} // namespace ms::dsp
