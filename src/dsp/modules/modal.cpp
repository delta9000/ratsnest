#include <algorithm>
#include <cmath>
#include <vector>
#include "dsp/modules.h"
#include "dsp/resonate.h"

// Code-derived from Mutable Instruments Rings (MIT), substantially adapted.
// See docs/attribution.md and THIRD_PARTY_NOTICES.md.

namespace ms::dsp {
namespace {

constexpr InputInfo kIns[] = {{"in", 0.f},          {"freq", 220.f},
                              {"v_oct", 0.f},       {"strike", 0.f},
                              {"structure", 0.25f}, {"brightness", 0.5f},
                              {"damping", 0.5f},    {"position", 0.25f}};
constexpr ConfigInfo kCfgs[] = {{"modes", nullptr}, {"ratios", nullptr}};
constexpr OutputInfo kOuts[] = {{"out"}, {"odd"}, {"even"}};
// Coefficients refresh at block rate, Rings-style (24-sample blocks there).
constexpr int kBlock = 32;

// NaN-proof clamp: !(x >= lo) also catches NaN (delay.cpp precedent).
float sane(float x, float lo, float hi) {
  if (!(x >= lo)) return lo;
  return x > hi ? hi : x;
}

bool has_cfg(const Config& cfg, std::string_view name) {
  for (const auto& [k, v] : cfg)
    if (k == name) return true;
  return false;
}

std::string validate_cfg(const Config& cfg) {
  if (!has_cfg(cfg, "ratios")) return {};
  if (has_cfg(cfg, "modes"))
    return "specify one of 'modes:' or 'ratios:', not both";
  const auto& r = cfg_arr(cfg, "ratios");
  if (r.empty()) return "'ratios:' needs a list with at least one entry";
  if (r.size() > 128) return "'ratios:' accepts at most 128 entries";
  for (double v : r)
    if (!(v > 0.0)) return "'ratios:' entries must be positive";
  return {};
}

class Modal final : public Module {
 public:
  void init(double rate, const Config& cfg) override {
    rate_ = static_cast<float>(rate);
    ratios_.clear();
    for (double v : cfg_arr(cfg, "ratios"))
      ratios_.push_back(std::min(static_cast<float>(v), 64.0f));
    int m;
    if (ratios_.empty()) {
      m = static_cast<int>(cfg_num(cfg, "modes", 64.0));
      m = std::clamp(m, 2, 128);
    } else {
      m = static_cast<int>(ratios_.size());
    }
    m += m & 1;  // Rings requires an even resolution; an odd ratios count
                 // pads with an inert mode parked at the 0.49 ceiling.
    modes_.assign(static_cast<std::size_t>(m), resonate::Svf{});
    exciter_.reset();
    num_active_ = 0;
    countdown_ = 0;
    position_ = 0.25f;
    position_step_ = 0.0f;
    fc_ = 0.01f;
    prev_strike_ = 0.0f;
  }

  void process(const float* ins, float* outs) override {
    if (countdown_ == 0) update_block(ins);
    --countdown_;

    const bool strike = ins[3] > 0.5f && prev_strike_ <= 0.5f;
    prev_strike_ = ins[3];

    // The bank is a feedback-free but long-ringing structure; one hostile
    // sample would ring as NaN forever.
    float exc = ins[0];
    if (!(exc > -1e6f && exc < 1e6f)) exc = 0.0f;
    // Filtered impulse, energy-normalized against the exciter cutoff
    // (part.cc: 0.25 * 2^(fc^2 * 24 / 12) / fc).
    if (strike)
      exc += 0.25f * resonate::semitones_to_ratio(fc_ * fc_ * 24.0f) / fc_;
    const float input = exciter_.process_lp(exc) * 0.125f;

    position_ += position_step_;
    resonate::CosineSeries amps(position_);
    float odd = 0.0f;
    float even = 0.0f;
    for (int i = 0; i < num_active_; i += 2) {
      odd += amps.next() *
             modes_[static_cast<std::size_t>(i)].process_bp(input);
      even += amps.next() *
              modes_[static_cast<std::size_t>(i + 1)].process_bp(input);
    }
    outs[0] = odd + even;
    outs[1] = odd;
    outs[2] = even;
  }

  void copy_state_from(const Module& other) override {
    const auto& o = static_cast<const Modal&>(other);
    const std::size_t n = std::min(modes_.size(), o.modes_.size());
    std::copy_n(o.modes_.begin(), n, modes_.begin());
    exciter_ = o.exciter_;
    position_ = o.position_;
    position_step_ = o.position_step_;
    fc_ = o.fc_;
    prev_strike_ = o.prev_strike_;
    num_active_ = std::min(o.num_active_, static_cast<int>(modes_.size()));
    countdown_ = 0;  // recompute coefficients for this instance's bank size
  }

 private:
  void update_block(const float* ins) {
    countdown_ = kBlock;
    const float freq = sane(ins[1], 0.0f, 24000.0f);
    const float voct = sane(ins[2], -10.0f, 10.0f);
    float f = freq * std::exp2(voct) / rate_;
    f = sane(f, 1e-4f, 0.25f);

    const float structure = sane(ins[4], 0.0f, 0.9995f);
    const float b_knob = sane(ins[5], 0.0f, 1.0f);
    const float damping = sane(ins[6], 0.0f, 1.0f);
    const float pos = sane(ins[7], 0.0f, 1.0f);

    // Mode placement and Q ladder (resonator.cc ComputeFilters).
    float stiffness = resonate::stiffness_curve(structure);
    float harmonic = f;
    float stretch = 1.0f;
    // 500 * 10^(4*damping): four decades of Q.
    float q = 500.0f * std::exp2(damping * 4.0f * 3.3219281f);
    float ba = 1.0f - structure;  // brightness attenuation (1-structure)^8
    ba *= ba;
    ba *= ba;
    ba *= ba;
    float b = b_knob * b_knob;  // Rings squares brightness at the part level
    b *= 1.0f - 0.2f * ba;
    float q_loss = b * (2.0f - b) * 0.85f + 0.15f;
    const float q_loss_rate = structure * (2.0f - structure) * 0.1f;
    int num = 0;
    const int m = static_cast<int>(modes_.size());
    const int nr = static_cast<int>(ratios_.size());
    // With explicit ratios, structure stretches/compresses the listed
    // series: f_i = f0 * ratios[i]^w, w = 1 at the 0.25 default so the
    // ratios sound exactly as written (spec 2026-07-18-drumkit §2).
    const float warp = std::exp2((structure - 0.25f) * 2.0f);
    for (int i = 0; i < m; ++i) {
      float pf;
      if (nr > 0) {
        pf = i < nr
                 ? f * std::pow(ratios_[static_cast<std::size_t>(i)], warp)
                 : 0.49f;
      } else {
        pf = harmonic * stretch;
      }
      if (pf >= 0.49f) {
        pf = 0.49f;
      } else {
        num = i + 1;
      }
      auto& mode = modes_[static_cast<std::size_t>(i)];
      mode.set_f_q_fast(pf, 1.0f + pf * q);
      mode.flush_denormals();
      stretch += stiffness;
      stiffness *= stiffness < 0.0f ? 0.93f : 0.98f;
      q_loss += q_loss_rate * (1.0f - q_loss);
      harmonic += f;
      q *= q_loss;
    }
    num_active_ = std::min(num + (num & 1), m);

    // Exciter lowpass: cutoff tracks pitch, +/-48 semitones on brightness
    // (unsquared knob), Q 1.5 (part.cc internal-exciter mapping).
    const float c = b_knob * (2.0f - b_knob);
    fc_ = std::clamp(f * resonate::semitones_to_ratio((c - 0.5f) * 96.0f),
                     1e-5f, 0.499f);
    exciter_.set_f_q_fast(fc_, 1.5f);
    exciter_.flush_denormals();

    position_step_ = (pos - position_) * (1.0f / kBlock);
  }

  float rate_ = 48000.0f;
  std::vector<float> ratios_;
  std::vector<resonate::Svf> modes_;
  resonate::Svf exciter_;
  int num_active_ = 0;
  int countdown_ = 0;
  float position_ = 0.25f;
  float position_step_ = 0.0f;
  float fc_ = 0.01f;
  float prev_strike_ = 0.0f;
};

std::unique_ptr<Module> make() { return std::make_unique<Modal>(); }
}  // namespace

const ModuleInfo modal_info{"modal", kIns, kCfgs, kOuts,
                            &make, false, false, &validate_cfg};
}  // namespace ms::dsp
