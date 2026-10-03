#include "dsp/mastering.h"
#include "dsp/modules.h"
#include <algorithm>
#include <cmath>

namespace ms::dsp {
namespace {
constexpr InputInfo kIns[] = {{"in_l", 0.f},    {"in_r", 0.f}, {"level", 0.f},
                              {"balance", 0.f}, {"mute", 0.f}, {"send", -96.f}};
constexpr const char *kSend[] = {"post", "pre", nullptr};
constexpr ConfigInfo kCfg[] = {{"send_mode", kSend}};
constexpr OutputInfo kOuts[] = {{"out_l"},  {"out_r"}, {"send_l"},
                                {"send_r"}, {"peak"},  {"rms"}};
float gain(float db) {
  return db <= -96 ? 0.f : db_to_linear(sanitize_clamp(db, -96, 24));
}
class Channel final : public Module {
public:
  void init(double rate, const Config &cfg) override {
    pre_ = cfg_str(cfg, "send_mode", "post") == "pre";
    for (auto *s : {&left_, &right_, &mute_, &send_})
      s->set_time(.005f, static_cast<float>(rate));
    decay_ = std::exp(-1.0 / (.3 * rate));
  }
  void process(const float *in, float *out) override {
    const float balance = sanitize_clamp(in[3], -1, 1);
    // Stereo balance, unity at center; mono sources should feed both inputs.
    const float g = gain(in[2]), l = g * (1 - std::max(0.f, balance)),
                r = g * (1 + std::min(0.f, balance));
    const float m = in[4] > .5f ? 0.f : 1.f, s = gain(in[5]);
    if (first_) {
      left_.init(l);
      right_.init(r);
      mute_.init(m);
      send_.init(s);
      first_ = false;
    }
    const float ml = mute_.process(m), sl = send_.process(s);
    const float a = sanitize(in[0]) * ml, b = sanitize(in[1]) * ml;
    out[0] = a * left_.process(l);
    out[1] = b * right_.process(r);
    out[2] = (pre_ ? a : out[0]) * sl;
    out[3] = (pre_ ? b : out[1]) * sl;
    peak_ = std::max(
        static_cast<double>(std::max(std::abs(out[0]), std::abs(out[1]))),
        peak_ * decay_);
    const double power = (static_cast<double>(out[0]) * out[0] +
                          static_cast<double>(out[1]) * out[1]) *
                         .5;
    power_ = power + (power_ - power) * decay_;
    out[4] = static_cast<float>(20 * std::log10(std::max(peak_, 1e-12)));
    out[5] = static_cast<float>(10 * std::log10(std::max(power_, 1e-24)));
  }
  void copy_state_from(const Module &other) override {
    const auto &o = static_cast<const Channel &>(other);
    left_.init(o.left_.value());
    right_.init(o.right_.value());
    mute_.init(o.mute_.value());
    send_.init(o.send_.value());
    first_ = o.first_;
    peak_ = o.peak_;
    power_ = o.power_;
  }

private:
  OnePole left_, right_, mute_, send_;
  double decay_ = 0, peak_ = 0, power_ = 0;
  bool pre_ = false, first_ = true;
};
std::unique_ptr<Module> make() { return std::make_unique<Channel>(); }
} // namespace
const ModuleInfo channel_info{"channel", kIns, kCfg, kOuts, &make};
} // namespace ms::dsp
