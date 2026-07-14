#include <algorithm>
#include <cmath>
#include <cstdint>
#include <numbers>
#include "dsp/modules.h"
#include "dsp/resonate.h"

namespace ms::dsp {
namespace {

constexpr InputInfo kIns[] = {{"in", 0.f},          {"freq", 220.f},
                              {"v_oct", 0.f},       {"strike", 0.f},
                              {"structure", 0.25f}, {"brightness", 0.5f},
                              {"damping", 0.5f},    {"position", 0.25f}};
constexpr ConfigInfo kCfgs[] = {{"seed", nullptr}};
constexpr OutputInfo kOuts[] = {{"out"}, {"aux"}};
constexpr int kBlock = 32;
// 8192-sample line: reaches ~5.9 Hz at 48 kHz, so Rings' sub-11.7 Hz
// linear upsampler is not needed. 4 guard samples for the Hermite read.
constexpr float kMaxDelay = 8188.0f;

float sane(float x, float lo, float hi) {
  if (!(x >= lo)) return lo;
  return x > hi ? hi : x;
}

class StringModule final : public Module {
 public:
  void init(double rate, const Config& cfg) override {
    rate_ = static_cast<float>(rate);
    line_.init(8192);
    stretch_.init(4096);  // holds up to 0.475 * kMaxDelay allpass samples
    fir_.reset();
    iir_.reset();
    exciter_.reset();
    const auto seed = static_cast<uint32_t>(cfg_num(cfg, "seed", 1.0));
    plucker_.init(seed);
    noise_rng_.seed(seed ^ 0x9e3779b9u);
    exc_dc_.init(1.0f - 10.0f / rate_);
    bridge_dc_.init(1.0f - 20.0f / rate_);
    delay_ = -1.0f;  // sentinel: snap interpolators on the first block
    delay_step_ = 0.0f;
    delay_target_ = 218.0f;
    position_ = 0.25f;
    position_step_ = 0.0f;
    dispersion_ = 0.0f;
    dispersion_step_ = 0.0f;
    comp_ = 1.0f;
    comp_step_ = 0.0f;
    f_ = 220.0f / rate_;
    exc_fc_ = 0.05f;
    noise_filter_ = 0.0f;
    pos_raw_ = 0.25f;
    dispersion_noise_ = 0.0f;
    curved_bridge_ = 0.0f;
    prev_strike_ = 0.0f;
    countdown_ = 0;
  }

  void process(const float* ins, float* outs) override {
    if (countdown_ == 0) update_block(ins);
    --countdown_;

    const bool strike = ins[3] > 0.5f && prev_strike_ <= 0.5f;
    prev_strike_ = ins[3];
    if (strike) {
      // Effective f0 from the clamped delay: hostile freq cannot request a
      // multi-minute noise burst.
      plucker_.trigger(1.0f / delay_target_, 8.0f * exc_fc_, pos_raw_);
    }

    // Per-sample interpolation of block-rate targets (zipper control).
    delay_ += delay_step_;
    position_ += position_step_;
    dispersion_ += dispersion_step_;
    comp_ += comp_step_;

    // Pickup tap uses the raw (uncompensated) delay, as in Rings.
    const float comb_delay =
        std::clamp(delay_ * position_, 0.0f, kMaxDelay);

    float delay = delay_ * comp_ - 1.0f;  // IIR + FIR phase compensation
    float s;
    {
      // Dispersion path runs unconditionally; at dispersion == 0 every term
      // vanishes and it reduces to a plain Hermite read (the RNG still
      // advances, keeping seeds comparable across structure values).
      const float d = dispersion_;
      float noise = noise_rng_.bipolar();
      noise *= 1.0f / (0.2f + noise_filter_);
      dispersion_noise_ += noise_filter_ * (noise - dispersion_noise_);

      const float stretch_point =
          d <= 0.0f ? 0.0f : d * (2.0f - d) * 0.475f;
      float noise_amount = d > 0.75f ? 4.0f * (d - 0.75f) : 0.0f;
      noise_amount = noise_amount * noise_amount * 0.025f;
      const float ac_amount = d < 0.0f ? -d : 0.0f;
      const float bridge_curving = ac_amount * ac_amount * 0.01f;
      const float ap_gain = -0.618f * d / (0.15f + std::abs(d));

      float delay_fm = 1.0f;
      delay_fm += dispersion_noise_ * noise_amount;
      delay_fm -= curved_bridge_ * bridge_curving;
      delay *= delay_fm;
      if (!(delay >= 4.0f)) delay = 4.0f;
      if (delay > kMaxDelay) delay = kMaxDelay;

      const float ap_delay = delay * stretch_point;
      const float main_delay = delay - ap_delay;
      if (ap_delay >= 4.0f && main_delay >= 4.0f) {
        s = line_.read_hermite(main_delay);
        s = stretch_.allpass(s, static_cast<std::size_t>(ap_delay), ap_gain);
      } else {
        s = line_.read_hermite(delay);
      }
      const float s_ac = bridge_dc_.process(s);
      s += ac_amount * (s_ac - s);
      // Curved-bridge nonlinearity: dead-zoned asymmetric rectifier whose
      // output FMs the delay on the next sample (gong/sitar behavior).
      const float value = std::abs(s) - 0.025f;
      const float sign = s > 0.0f ? 1.0f : -1.5f;
      curved_bridge_ = (std::abs(value) + value) * sign;
    }

    // Excitation: external audio (headroom-scaled, brightness lowpass)
    // plus the pluck burst, DC-blocked, summed into the loop.
    float exc = ins[0];
    if (!(exc > -1e6f && exc < 1e6f)) exc = 0.0f;
    exc = exciter_.process_lp(exc * 0.70710678f);
    exc += plucker_.process();
    exc = exc_dc_.process(exc);

    s += exc;
    s = fir_.process(s);
    s = iir_.process_lp(s);
    // Heal the loop: a NaN/inf write would otherwise circulate forever.
    if (!(s > -1e6f && s < 1e6f)) s = 0.0f;
    if (std::abs(s) < 1e-20f) s = 0.0f;  // denormal flush (render has no FTZ)
    line_.write(s);

    const float tap = line_.read_linear(comb_delay);
    outs[0] = s - tap;  // pluck-position comb (Rings' poly voice output)
    outs[1] = tap;
  }

  void copy_state_from(const Module& other) override {
    const auto& o = static_cast<const StringModule&>(other);
    line_.copy_from(o.line_);
    stretch_.copy_from(o.stretch_);
    fir_ = o.fir_;
    iir_ = o.iir_;
    exciter_ = o.exciter_;
    plucker_.copy_from(o.plucker_);
    noise_rng_ = o.noise_rng_;
    exc_dc_ = o.exc_dc_;
    bridge_dc_ = o.bridge_dc_;
    delay_ = o.delay_;
    delay_step_ = o.delay_step_;
    delay_target_ = o.delay_target_;
    position_ = o.position_;
    position_step_ = o.position_step_;
    dispersion_ = o.dispersion_;
    dispersion_step_ = o.dispersion_step_;
    comp_ = o.comp_;
    comp_step_ = o.comp_step_;
    f_ = o.f_;
    exc_fc_ = o.exc_fc_;
    noise_filter_ = o.noise_filter_;
    pos_raw_ = o.pos_raw_;
    dispersion_noise_ = o.dispersion_noise_;
    curved_bridge_ = o.curved_bridge_;
    prev_strike_ = o.prev_strike_;
    countdown_ = 0;
  }

 private:
  void update_block(const float* ins) {
    countdown_ = kBlock;
    const float freq = sane(ins[1], 0.0f, 24000.0f);
    const float voct = sane(ins[2], -10.0f, 10.0f);
    float f = freq * std::exp2(voct) / rate_;
    // Clamp through the delay line's reach: [4, kMaxDelay] samples.
    f = sane(f, 1.0f / kMaxDelay, 0.25f);
    f_ = f;
    delay_target_ = 1.0f / f;

    const float structure = sane(ins[4], 0.0f, 0.9995f);
    const float b_knob = sane(ins[5], 0.0f, 1.0f);
    const float damping = sane(ins[6], 0.0f, 1.0f);
    pos_raw_ = sane(ins[7], 0.0f, 1.0f);
    const float folded = 0.5f - 0.98f * std::abs(pos_raw_ - 0.5f);

    // Loop gain from target RT60 (string.cc): decay time exponential in
    // the damping knob, 0.07 s .. ~18 s.
    const float lf = damping * (2.0f - damping);
    const float rt60 = 0.07f * resonate::semitones_to_ratio(lf * 96.0f) * rate_;
    float g_loop = resonate::semitones_to_ratio(
        std::max(-120.0f * delay_target_ / rt60, -127.0f));
    float b = b_knob * b_knob;
    noise_filter_ = resonate::semitones_to_ratio((b_knob - 1.0f) * 48.0f);
    float damping_cutoff = std::min(
        24.0f + damping * damping * 48.0f + b_knob * b_knob * 24.0f, 84.0f);
    float damping_f =
        std::min(f * resonate::semitones_to_ratio(damping_cutoff), 0.499f);

    // Crossfade to a lossless loop at the top of the damping range.
    if (damping >= 0.95f) {
      const float to_inf = 20.0f * (damping - 0.95f);
      g_loop += to_inf * (1.0f - g_loop);
      b += to_inf * (1.0f - b);
      damping_f += to_inf * (0.4999f - damping_f);
      damping_cutoff += to_inf * (128.0f - damping_cutoff);
    }

    fir_.configure(g_loop, b, kBlock);
    iir_.set_f_q_accurate(damping_f, 0.5f);
    iir_.flush_denormals();
    exciter_.flush_denormals();
    if (std::abs(dispersion_noise_) < 1e-20f) dispersion_noise_ = 0.0f;
    if (std::abs(curved_bridge_) < 1e-20f) curved_bridge_ = 0.0f;

    // IIR phase-delay compensation: delay *= 1 - 2*atan(2^(-n/12))/(2*pi).
    const float shift =
        2.0f * std::atan(resonate::semitones_to_ratio(-damping_cutoff)) /
        (2.0f * std::numbers::pi_v<float>);
    const float comp_target = 1.0f - shift;

    const float disp_target = resonate::dispersion_from_structure(structure);

    // Exciter lowpass: same f-tracking brightness mapping as modal.
    const float c = b_knob * (2.0f - b_knob);
    exc_fc_ = std::clamp(f * resonate::semitones_to_ratio((c - 0.5f) * 96.0f),
                         1e-5f, 0.499f);
    exciter_.set_f_q_fast(exc_fc_, 1.5f);

    if (delay_ < 0.0f) {  // first block: snap, no glide from the sentinel
      delay_ = delay_target_;
      position_ = folded;
      dispersion_ = disp_target;
      comp_ = comp_target;
    }
    constexpr float inv = 1.0f / kBlock;
    delay_step_ = (delay_target_ - delay_) * inv;
    position_step_ = (folded - position_) * inv;
    dispersion_step_ = (disp_target - dispersion_) * inv;
    comp_step_ = (comp_target - comp_) * inv;
  }

  float rate_ = 48000.0f;
  resonate::DelayLine line_;
  resonate::DelayLine stretch_;
  resonate::DampingFir fir_;
  resonate::Svf iir_;
  resonate::Svf exciter_;
  resonate::Plucker plucker_;
  resonate::Rng noise_rng_{1u};
  resonate::DcBlocker exc_dc_;
  resonate::DcBlocker bridge_dc_;
  float delay_ = -1.0f;
  float delay_step_ = 0.0f;
  float delay_target_ = 218.0f;
  float position_ = 0.25f;
  float position_step_ = 0.0f;
  float dispersion_ = 0.0f;
  float dispersion_step_ = 0.0f;
  float comp_ = 1.0f;
  float comp_step_ = 0.0f;
  float f_ = 0.0f;
  float exc_fc_ = 0.05f;
  float noise_filter_ = 0.0f;
  float pos_raw_ = 0.25f;
  float dispersion_noise_ = 0.0f;
  float curved_bridge_ = 0.0f;
  float prev_strike_ = 0.0f;
  int countdown_ = 0;
};

std::unique_ptr<Module> make() { return std::make_unique<StringModule>(); }
}  // namespace

const ModuleInfo string_info{"string", kIns, kCfgs, kOuts, &make};
}  // namespace ms::dsp
