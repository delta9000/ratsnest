#include <algorithm>
#include <cmath>
#include "dsp/input_key.h"
#include "dsp/modules.h"
#include "dsp/mastering.h"

// Lookahead brickwall after Geraint Luff, "Designing a straightforward
// limiter" (Signalsmith Audio, 2022): windowed peak-hold of the required gain
// reduction, release smoothing, then a box filter as long as the lookahead.
// `true_peak: on` detects 4x-interpolated peaks (ITU-R BS.1770-4 Annex 2
// style, dsp/mastering.h). See docs/attribution.md.

namespace ms::dsp {
namespace {

constexpr InputInfo kIns[] = {
    {"in_l", 0.0f},
    {"in_r", 0.0f},
    {"threshold", -0.1f}, // dBFS
    in_secs("release", 0.05f)    // s
};
// The first entry is the default: true-peak detection unless asked not to.
constexpr const char* kOnOff[] = {"on", "off", nullptr};
constexpr ConfigInfo kCfg[] = {{"true_peak", kOnOff}};
constexpr OutputInfo kOuts[] = {
    {"out_l"},
    {"out_r"},
    {"gain"},
    {"gr"}
};

constexpr int kRing = 512;  // audio + box-filter history; lookahead < kRing

class Limiter final : public Module {
 public:
  void init(double rate, const Config& cfg) override {
    rate_ = static_cast<float>(rate);
    // 2.0 ms lookahead; the box filter spans the lookahead plus the current
    // sample, so it has fully settled by the time a peak leaves the delay.
    // The true-peak detector reports a peak kLag samples late, so the audio
    // waits that much longer, and the hold covers both samples around it.
    tp_ = cfg_enum(cfg, "true_peak", kOnOff) == 0;
    look_ = std::clamp(static_cast<int>(std::round(rate_ * 0.002f)), 1, kRing - 2 - TruePeak4x::kLag);
    delay_ = look_ + TruePeak4x::kLag * tp_;
    st_ = State{};
  }

  void process(const float* ins, float* outs) override {
    State& s = st_;
    const float in_l = sanitize(ins[0], 0.0f);
    const float in_r = sanitize(ins[1], 0.0f);
    const float threshold = sanitize_clamp(ins[2], -24.0f, 0.0f, -0.1f);
    const float release = sanitize_clamp(ins[3], 0.001f, 2.0f, 0.05f);
    const int box = look_ + 1;
    const int w = s.write;
    s.buf_l[w] = in_l;
    s.buf_r[w] = in_r;

    // 1. Gain reduction (dB) each input sample needs, peak-held over the
    //    lookahead (or the bass-safe hold, if longer).
    const float peak = tp_ ? std::max(s.tp_l.push(in_l), s.tp_r.push(in_r))
                           : std::max(std::abs(in_l), std::abs(in_r));
    const float need = std::max(0.0f, linear_to_db(peak) - threshold);
    const float held = s.hold.push(need, std::max(box + tp_, hold_samples(release, rate_)));

    // 2. Instant attack, one-pole release.
    if (release_key_.changed({release})) alpha_r_ = 1.0f - std::exp(-1.0f / (release * rate_));
    s.env = held >= s.env ? held : flush_denormal(s.env + alpha_r_ * (held - s.env));

    // 3. Box filter over `box` samples: every term covering a peak is at least
    //    its requirement, so the mean is too when the peak leaves the delay.
    s.sum += s.env - s.box[(w - box + kRing) % kRing];
    s.box[w] = s.env;
    const float gr = static_cast<float>(std::max(0.0, s.sum / box));
    if (gain_key_.changed({gr})) g_linear_ = db_to_linear(-gr);

    const int r = (w - delay_ + kRing) % kRing;
    outs[0] = s.buf_l[r] * g_linear_;
    outs[1] = s.buf_r[r] * g_linear_;
    outs[2] = g_linear_;
    outs[3] = gr;
    s.write = (w + 1) % kRing;
  }

  void copy_state_from(const Module& other) override {
    st_ = static_cast<const Limiter&>(other).st_;
  }

 private:
  struct State {
    float buf_l[kRing] = {}, buf_r[kRing] = {}, box[kRing] = {};
    SlidingMax<4096> hold;  // 20 ms up to 192 kHz
    TruePeak4x tp_l, tp_r;
    float env = 0.0f;
    double sum = 0.0;       // running box sum; double keeps drift negligible
    int write = 0;
  };
  float rate_ = 48000.0f;
  int look_ = 96, delay_ = 96;
  bool tp_ = false;
  State st_;
  // Memoized libm results; see dsp/input_key.h.
  InputKey<1> release_key_, gain_key_;
  float alpha_r_ = 0.0f, g_linear_ = 1.0f;
};

std::unique_ptr<Module> make() { return std::make_unique<Limiter>(); }

}  // namespace

const ModuleInfo limiter_info{"limiter", kIns, kCfg, kOuts, &make};

}  // namespace ms::dsp
