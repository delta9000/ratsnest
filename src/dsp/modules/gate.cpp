#include <algorithm>
#include <cmath>
#include "dsp/input_key.h"
#include "dsp/mastering.h"
#include "dsp/modules.h"

// Downward expander / noise gate: the compressor's log-domain detector and
// branching ballistics (Giannoulis, Massberg and Reiss 2012) turned below
// the threshold, with a level hold. See docs/attribution.md.

namespace ms::dsp {
namespace {

constexpr InputInfo kIns[] = {
    {"in_l", 0.0f},       {"in_r", 0.0f},     {"side_l", 0.0f},   {"side_r", 0.0f},
    {"threshold", -40.0f},  // dBFS
    {"ratio", 10.0f},       // dB of cut per dB under the threshold
    {"range", 60.0f},       // most cut, dB
    in_secs("attack", 0.001f),     // s, opening
    in_secs("hold", 0.02f),        // s
    in_secs("release", 0.1f)};     // s, closing
constexpr const char* kSidechains[] = {"internal", "external", nullptr};
constexpr ConfigInfo kCfg[] = {{"sidechain", kSidechains}};
constexpr OutputInfo kOuts[] = {{"out_l"}, {"out_r"}, {"gain"}, {"gr"}, {"open"}};
constexpr unsigned kHoldCap = 16384;  // 0.25 s up to 64 kHz, 85 ms at 192 kHz

class Gate final : public Module {
 public:
  void init(double rate, const Config& cfg) override {
    rate_ = static_cast<float>(rate);
    external_ = cfg_enum(cfg, "sidechain", kSidechains) == 1;
    gr_ = 0.0f;
    hold_ = {};
  }

  void process(const float* ins, float* outs) override {
    const float in_l = sanitize(ins[0], 0.0f), in_r = sanitize(ins[1], 0.0f);
    const float threshold = sanitize_clamp(ins[4], -120.0f, 0.0f, -40.0f);
    const float ratio = sanitize_clamp(ins[5], 1.0f, 1000.0f, 10.0f);
    const float range = sanitize_clamp(ins[6], 0.0f, 120.0f, 60.0f);
    const float attack = sanitize_clamp(ins[7], 0.0001f, 10.0f, 0.001f);
    const float hold = sanitize_clamp(ins[8], 0.0f, 0.25f, 0.02f);
    const float release = sanitize_clamp(ins[9], 0.0001f, 10.0f, 0.1f);

    // 1. Stereo-linked peak level, held so the gate does not chatter
    //    through a waveform's zero crossings or a short dip.
    const float det = external_ ? std::max(std::abs(sanitize(ins[2], 0.0f)),
                                           std::abs(sanitize(ins[3], 0.0f)))
                                : std::max(std::abs(in_l), std::abs(in_r));
    const float level = hold_.push(linear_to_db(det), std::max(1, static_cast<int>(hold * rate_)));

    // 2. Below the threshold every dB under becomes `ratio` dB, up to `range`.
    const float target = std::min(range, std::max(0.0f, threshold - level) * (ratio - 1.0f));

    // 3. Opening (less cut) at `attack`, closing at `release`.
    if (time_key_.changed({attack, release})) {
      alpha_a_ = 1.0f - std::exp(-1.0f / (attack * rate_));
      alpha_r_ = 1.0f - std::exp(-1.0f / (release * rate_));
    }
    gr_ = flush_denormal(gr_ + (target < gr_ ? alpha_a_ : alpha_r_) * (target - gr_));
    if (gain_key_.changed({gr_})) g_ = db_to_linear(-gr_);

    outs[0] = in_l * g_;
    outs[1] = in_r * g_;
    outs[2] = g_;
    outs[3] = gr_;
    outs[4] = level >= threshold ? 1.0f : 0.0f;
  }

  void copy_state_from(const Module& other) override {
    const auto& o = static_cast<const Gate&>(other);
    gr_ = o.gr_;
    hold_ = o.hold_;
  }

 private:
  float rate_ = 48000.0f;
  bool external_ = false;
  float gr_ = 0.0f;
  SlidingMax<kHoldCap> hold_;
  InputKey<2> time_key_;
  InputKey<1> gain_key_;
  float alpha_a_ = 0.0f, alpha_r_ = 0.0f, g_ = 1.0f;
};

std::unique_ptr<Module> make() { return std::make_unique<Gate>(); }
}  // namespace

const ModuleInfo gate_info{"gate", kIns, kCfg, kOuts, &make};
}  // namespace ms::dsp
