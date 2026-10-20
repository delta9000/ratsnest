#include <algorithm>
#include <cmath>
#include <numbers>
#include <string>
#include "dsp/modules.h"
#include "dsp/biquad.h"
#include "dsp/mastering.h"
#include "dsp/oversample.h"
#include "dsp/waveshape.h"

namespace ms::dsp {
namespace {

constexpr InputInfo kIns[] = {{"in", 0.f},
                              {"drive", 0.5f},
                              {"tone", 0.5f},
                              {"level", 0.5f},
                              {"mix", 1.0f}};
constexpr const char* kModels[] = {"tubescreamer", "klon", "rat",  "ds1",
                                   "mxr",          "muff", "clean", nullptr};
constexpr ConfigInfo kCfg[] = {{"model", kModels}};
constexpr OutputInfo kOuts[] = {{"out"}};
constexpr int kBlock = 32;

using waveshape::Curve;

// A pedal voicing. Constants are approximate, after Yeh DAFx-07 and the
// ElectroSmash circuit analyses (spec §5).
struct Voice {
  float pre_hp;      // pre-emphasis highpass (Hz); 0 = bypass
  Curve curve;       // clip curve
  float asym;        // clip bias (asymmetry)
  float drive_span;  // octaves of pre-gain across drive 0..1
  float tone_lo;     // tone lowpass corner at tone=0 (Hz)
  float tone_hi;     // tone lowpass corner at tone=1 (Hz)
  float scoop_freq;  // mid-scoop peaking cut center (Hz); 0 = none
  float scoop_db;    // mid-scoop gain (dB, <= 0)
  bool two_stage;    // Big Muff: two cascaded shapers
  float inter_lp;    // inter-stage lowpass (Hz), only if two_stage
  float post_lp;     // post-shape one-pole lowpass (Hz); 0 = none (RAT slew)
};

Voice voice_for(const std::string& m) {
  // drive_span in octaves of pre-gain across drive 0..1 (drive 0.5 -> 2^(span/2)).
  // Ranges run hot so a decaying source keeps grinding, not just the attack.
  //          pre_hp curve         asym  span  tlo    thi     scpF   scpDB  2st   inter  post
  if (m == "klon")
    return {340.f, Curve::Soft, 0.05f, 9.f, 3000.f, 8000.f, 0.f, 0.f, false, 0.f, 0.f};
  if (m == "rat")
    return {90.f, Curve::Hard, 0.05f, 14.f, 500.f, 6000.f, 0.f, 0.f, false, 0.f, 6000.f};
  if (m == "ds1")
    return {1100.f, Curve::Soft, 0.15f, 12.f, 800.f, 3000.f, 500.f, -8.f, false, 0.f, 0.f};
  if (m == "mxr")
    return {160.f, Curve::Soft, 0.12f, 11.f, 5000.f, 16000.f, 0.f, 0.f, false, 0.f, 0.f};
  if (m == "muff")
    return {120.f, Curve::Soft, 0.0f, 13.f, 1500.f, 4000.f, 800.f, -6.f, true, 1200.f, 0.f};
  if (m == "clean")
    return {0.f, Curve::Soft, 0.0f, 7.f, 2000.f, 12000.f, 0.f, 0.f, false, 0.f, 0.f};
  // tubescreamer (default / unknown model)
  return {720.f, Curve::Soft, 0.12f, 10.f, 2000.f, 6000.f, 0.f, 0.f, false, 0.f, 0.f};
}

// NaN-proof clamp for unipolar control inputs.
float sane01(float x) {
  if (!(x >= 0.0f)) return 0.0f;
  return x > 1.0f ? 1.0f : x;
}

class Overdrive final : public Module {
 public:
  void init(double rate, const Config& cfg) override {
    rate_ = static_cast<float>(rate);
    const float os_rate = rate_ * 4.0f;
    voice_ = voice_for(cfg_str(cfg, "model", "tubescreamer"));

    pre_state_.clear();
    tone_state_.clear();
    scoop_state_.clear();
    inter_state_.clear();
    shaper1_.reset();
    shaper2_.reset();
    shaper1_.configure(voice_.curve, voice_.asym);
    shaper2_.configure(voice_.curve, voice_.asym);
    oversampler_.clear();
    decimator_.clear();
    dc_x1_ = dc_y1_ = 0.0f;
    clean_z1_ = 0.0f;

    const float pi = std::numbers::pi_v<float>;
    post_.set_time(voice_.post_lp > 0.f ? 1.0f / (2.0f * pi * voice_.post_lp)
                                        : 0.0f,
                   rate_);
    post_.init(0.0f);
    mix_.set_time(0.005f, rate_);
    mix_.init(1.0f);
    level_.set_time(0.005f, rate_);
    level_.init(1.0f);

    pre_coeffs_ =
        voice_.pre_hp > 0.f ? make_hpf(voice_.pre_hp, rate_) : make_bypass();
    scoop_coeffs_ = voice_.scoop_db < 0.f
                        ? make_peaking_eq(voice_.scoop_freq, voice_.scoop_db,
                                          1.0f, rate_)
                        : make_bypass();
    inter_coeffs_ =
        voice_.two_stage ? make_lpf(voice_.inter_lp, os_rate) : make_bypass();
    tone_coeffs_ = make_lpf(voice_.tone_hi, rate_);

    drive_gain_ = 1.0f;
    mix_target_ = 1.0f;
    level_target_ = 1.0f;
    countdown_ = 0;
  }

  void process(const float* ins, float* outs) override {
    if (countdown_ == 0) update_block(ins);
    --countdown_;

    // Bounded far above any audio level so the ADAA antiderivative's x*x
    // cannot overflow to inf and poison the oversampler and filter states.
    const float in0 = sanitize_clamp(ins[0], -1e6f, 1e6f);
    const float x = in0 * drive_gain_;
    const float xf = Biquad::process(x, pre_coeffs_, pre_state_);

    // Clean tap at input level (pre-drive), delayed one sample to align with
    // the oversampler round trip (Decimator4x peaks at 1-sample latency, as in
    // the clipper). Input-level clean keeps `mix` a true dry/wet crossfade
    // rather than a volume control (how a Klon/TS blends clean + clipped).
    const float clean = clean_z1_;
    clean_z1_ = in0;

    float up[4];
    oversampler_.upsample(xf, up);
    float sh[4];
    for (int i = 0; i < 4; ++i) sh[i] = shape_os(up[i]);
    float w = decimator_.downsample(sh);

    // DC blocker: asymmetric clipping injects DC.
    const float dcy = w - dc_x1_ + 0.9995f * dc_y1_;
    dc_x1_ = w;
    dc_y1_ = flush_denormal(dcy);
    w = dcy;

    if (voice_.post_lp > 0.f) w = post_.process(w);

    const float mix = mix_.process(mix_target_);
    float y = mix * w + (1.0f - mix) * clean;
    y = Biquad::process(y, scoop_coeffs_, scoop_state_);
    y = Biquad::process(y, tone_coeffs_, tone_state_);
    outs[0] = y * level_.process(level_target_);
  }

  void copy_state_from(const Module& other) override {
    const auto& o = static_cast<const Overdrive&>(other);
    pre_state_ = o.pre_state_;
    tone_state_ = o.tone_state_;
    scoop_state_ = o.scoop_state_;
    inter_state_ = o.inter_state_;
    shaper1_ = o.shaper1_;
    shaper2_ = o.shaper2_;
    oversampler_ = o.oversampler_;
    decimator_ = o.decimator_;
    dc_x1_ = o.dc_x1_;
    dc_y1_ = o.dc_y1_;
    post_ = o.post_;
    mix_ = o.mix_;
    level_ = o.level_;
    clean_z1_ = o.clean_z1_;
    drive_gain_ = o.drive_gain_;
    tone_coeffs_ = o.tone_coeffs_;
    mix_target_ = o.mix_target_;
    level_target_ = o.level_target_;
    countdown_ = 0;
  }

 private:
  float shape_os(float x) {
    float y = shaper1_.process(x);
    if (voice_.two_stage) {
      y = Biquad::process(y, inter_coeffs_, inter_state_);
      y = shaper2_.process(y);
    }
    return y;
  }

  void update_block(const float* ins) {
    countdown_ = kBlock;
    const float drive = sane01(ins[1]);
    const float tone = sane01(ins[2]);
    const float level = sane01(ins[3]);
    mix_target_ = sane01(ins[4]);

    drive_gain_ = std::exp2(drive * voice_.drive_span);
    level_target_ = std::exp2((level - 0.5f) * 4.0f);  // +/-2 octaves makeup

    const float lo = std::log2(voice_.tone_lo);
    const float hi = std::log2(voice_.tone_hi);
    const float corner = std::exp2(lo + (hi - lo) * tone);
    tone_coeffs_ = make_lpf(std::min(corner, 0.45f * rate_), rate_);
  }

  float rate_ = 48000.0f;
  Voice voice_{};
  BiquadCoeffs pre_coeffs_{}, tone_coeffs_{}, scoop_coeffs_{}, inter_coeffs_{};
  BiquadState pre_state_{}, tone_state_{}, scoop_state_{}, inter_state_{};
  waveshape::Shaper shaper1_, shaper2_;
  Oversampler4x oversampler_;
  Decimator4x decimator_;
  float dc_x1_ = 0.0f, dc_y1_ = 0.0f;
  OnePole post_, mix_, level_;
  float clean_z1_ = 0.0f;
  float drive_gain_ = 1.0f;
  float mix_target_ = 1.0f;
  float level_target_ = 1.0f;
  int countdown_ = 0;
};

std::unique_ptr<Module> make() { return std::make_unique<Overdrive>(); }
}  // namespace

const ModuleInfo overdrive_info{"overdrive", kIns, kCfg, kOuts, &make};
}  // namespace ms::dsp
