#include <algorithm>
#include <cmath>
#include <numbers>
#include "dsp/bandlimited_osc.h"
#include "dsp/input_key.h"
#include "dsp/minblep.h"
#include "dsp/modules.h"
#include "dsp/util.h"

namespace ms::dsp {
namespace {

constexpr InputInfo kIns[] = {{"freq", 220.f}, {"v_oct", 0.f}, {"fm", 0.f},
                              {"pw", 0.5f},    {"pwm", 0.f},   {"sync", 0.f},
                              {"phase", 0.f}};
constexpr const char* kWaves[] = {"sine", "tri", "saw", "pulse", nullptr};
constexpr ConfigInfo kCfg[] = {{"wave", kWaves}};
constexpr OutputInfo kOuts[] = {{"out"}};

class Vco final : public Module {
 public:
  void init(double rate, const Config& cfg) override {
    rate_ = static_cast<float>(rate);
    const std::string w = cfg_str(cfg, "wave", "sine");
    wave_ = w == "tri" ? 1 : w == "saw" ? 2 : w == "pulse" ? 3 : 0;
    phase_ = 0.0f;
    sync_edge_ = RisingEdge{};
    bank_ = &BandlimitedOsc::instance();
    MinBlep::instance();  // build the table here, never on the audio thread
    blep_ = false;
    ring_.clear();
    last_ = 0.0f;
  }

  void process(const float* ins, float* outs) override {
    if (key_.changed({ins[0], ins[1], ins[2]})) {  // skip exp2 at a held pitch
      double f = static_cast<double>(ins[0]) * std::exp2(static_cast<double>(ins[1])) +
                 static_cast<double>(ins[2]) * static_cast<double>(ins[0]);
      if (!std::isfinite(f)) f = 0.0;
      f = std::clamp(f, 0.0, 0.45 * static_cast<double>(rate_));
      dt_ = f / static_cast<double>(rate_);
      band_ = bank_->band(static_cast<float>(f), rate_);
    }
    const double dt = dt_;

    const float raw_pw = ins[3] + ins[4];
    const float pw = std::isfinite(raw_pw) ? std::clamp(raw_pw, 0.05f, 0.95f) : 0.5f;
    const double offset = std::isfinite(ins[6]) ? static_cast<double>(ins[6]) : 0.0;

    // Hard sync. The master's edge crossed 0.5 somewhere between the last
    // sample and this one; linear interpolation puts it at `frac` (0..1)
    // into the interval. The slave restarts there, so at this sample it is
    // (1 - frac) samples into its new cycle: the reset keeps sub-sample
    // timing instead of jittering to the sample grid.
    const float sync = std::isfinite(ins[5]) ? ins[5] : 0.0f;
    const float prev_sync = sync_edge_.prev;
    if (sync_edge_(sync)) {
      const float frac = std::clamp((0.5f - prev_sync) / (sync - prev_sync), 0.0f, 1.0f);
      if (!blep_) {  // first sync: from here on, naive shapes + minBLEP
        blep_ = true;
        ring_.clear();
      }
      // The restart is a step from where the slave was to the start of
      // its cycle, (1 - frac) samples ago.
      const float before = naive(wrap(phase_ + frac * dt + offset), pw);
      phase_ = (1.0 - frac) * dt;
      last_ = wrap(offset);
      ring_.step(naive(last_, pw) - before, static_cast<float>(1.0 - frac));
    } else {
      // Advance first (double phase): a k-Hz tone then completes exactly k
      // cycles over rate samples.
      phase_ += dt;
      phase_ -= std::floor(phase_);
    }
    const float ph = wrap(phase_ + offset);
    if (!blep_) {
      outs[0] = shape(ph, pw);
      return;
    }
    steps(ph, pw);
    outs[0] = naive(ph, pw) + ring_.next();
  }

  // A synced vco runs naive waveforms and band-limits every step (the
  // restarts above, and the saw's and pulse's own edges here) with a
  // minimum-phase BLEP (dsp/minblep.h): the tables cannot be restarted
  // mid-cycle without a half-drawn edge. A 1712 Hz saw synced to a 440 Hz
  // sine keeps its inharmonic (aliased) energy at -62 dB of the total; it
  // was -15 dB with a bare reset. A band-limited pulse master's edge rings,
  // so its crossing places the restart less exactly: -29 dB.
  void steps(float ph, float pw) {
    float inc = ph - last_;
    if (inc < -0.5f) inc += 1.0f;  // wrapped since the last sample
    const float from = last_;
    last_ = ph;
    if (!(inc > 1e-9f) || wave_ == 0 || wave_ == 1) return;  // sine, tri: no edges
    const float to = from + inc;  // unwrapped
    if (to >= 1.0f) ring_.step(wave_ == 2 ? -2.0f : 2.0f, (to - 1.0f) / inc);
    if (wave_ == 3)
      for (float edge : {pw, pw + 1.0f})
        if (from < edge && edge <= to) ring_.step(-2.0f, (to - edge) / inc);
  }

  // The ideal (not band-limited) waveforms the tables approximate.
  float naive(float ph, float pw) const {
    switch (wave_) {
      case 1: return 4.0f * std::abs(ph - 0.5f) - 1.0f;
      case 2: return 2.0f * ph - 1.0f;
      case 3: return ph < pw ? 1.0f : -1.0f;
      default: return std::sin(2.0f * std::numbers::pi_v<float> * ph);
    }
  }

  static float wrap(double ph) { return static_cast<float>(ph - std::floor(ph)); }

  // The waveform at phase ph (0..1) for the current band.
  float shape(float ph, float pw) const {
    switch (wave_) {
      case 1:  // band-limited triangle table
        return bank_->triangle(ph, band_);
      case 2:  // band-limited saw table
        return bank_->saw(ph, band_);
      case 3: {  // difference of two band-limited saws, plus exact DC term
        float shifted = ph - pw;
        shifted -= std::floor(shifted);
        return bank_->saw(shifted, band_) - bank_->saw(ph, band_) + 2.0f * pw - 1.0f;
      }
      default:  // sine
        return std::sin(2.0f * std::numbers::pi_v<float> * ph);
    }
  }

  void copy_state_from(const Module& other) override {
    const auto& o = static_cast<const Vco&>(other);
    phase_ = o.phase_;
    sync_edge_ = o.sync_edge_;
    blep_ = o.blep_;
    ring_ = o.ring_;
    last_ = o.last_;
  }

 private:
  float rate_ = 48000.0f;
  int wave_ = 0;
  double phase_ = 0.0;
  bool blep_ = false;  // has been synced: naive shapes + minBLEP
  BlepRing ring_;
  float last_ = 0.0f;  // last sample's phase (with offset), for edge times
  RisingEdge sync_edge_;
  const BandlimitedOsc* bank_ = nullptr;
  InputKey<3> key_;  // freq, v_oct, fm -> dt_, band_
  double dt_ = 0.0;
  BandlimitedOsc::Band band_{};
};

std::unique_ptr<Module> make() { return std::make_unique<Vco>(); }
}  // namespace

const ModuleInfo vco_info{"vco", kIns, kCfg, kOuts, &make};
}  // namespace ms::dsp
