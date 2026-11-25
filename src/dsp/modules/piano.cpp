#include <algorithm>
#include <array>
#include <cmath>
#include "dsp/modules.h"
#include "dsp/piano_model.h"
#include "dsp/util.h"

// Physical piano voice: felt hammer -> 1..3 detuned, coupled stiff strings.
// Paper-derived / concept-inspired; see src/dsp/piano_model.h and
// docs/attribution.md ("piano").

namespace ms::dsp {
namespace {

using piano::KeyDesign;
using piano::kKeys;

constexpr InputInfo kIns[] = {{"gate", 0.f, false}, {"voct", 0.f},  {"vel", 0.f},
                              {"hardness", 0.5f},   {"detune", 1.f}, {"decay", 1.f},
                              {"stretch", 1.f},     {"pedal", 0.f},  {"noise", 1.f}};
constexpr ConfigInfo kCfgs[] = {{"seed", nullptr}};
constexpr OutputInfo kOuts[] = {{"out"}};
// Output scale: bridge force (N) to signal level, set so a mezzo-forte C4
// peaks near 0.1 (-20 dBFS) (docs/modules.md "piano").
constexpr float kOutGain = 0.0035f;

class PianoModule final : public Module {
 public:
  void init(double rate, const Config& cfg) override {
    fs_ = static_cast<float>(rate);
    std::uint32_t rng = cfg_seed(cfg);
    for (int k = 0; k < kKeys; ++k) design_[k] = piano::design_key(k, rate, rng);
    max_len_ = static_cast<int>(rate / 20.0);
    for (auto& s : str_) s.init(static_cast<std::size_t>(max_len_));
    damp_rate_ = 1.0f - std::exp(-1.0f / (0.008f * fs_));
    env_fall_ = std::exp(-1.0f / (0.05f * fs_));
    mech_.init(fs_);
  }

  void process(const float* ins, float* outs) override {
    const bool held = ins[0] > 0.5f;
    if (edge_(ins[0])) strike(ins);
    if (was_held_ && !held && active_) release(ins);
    was_held_ = held;
    if (!active_) {
      outs[0] = declick_(0.0f);
      return;
    }
    const float tone = tick(held, ins[7]);
    env_ = std::max(std::abs(tone), env_ * env_fall_);
    const float y = tone + mech_.tick();
    if (!(std::abs(y) < 1e3f)) {  // heal a blown loop: never circulate NaN
      silence();
      outs[0] = 0.0f;
      return;
    }
    quiet_ = (hammer_.life > 0 || std::abs(y) > 1e-5f) ? 0 : quiet_ + 1;
    if (quiet_ > static_cast<int>(0.1f * fs_)) silence();
    last_ = declick_(y);
    outs[0] = last_;
  }

  void copy_state_from(const Module& other) override {
    const auto& o = static_cast<const PianoModule&>(other);
    for (int i = 0; i < piano::kMaxStrings; ++i) str_[i].copy_from(o.str_[i]);
    hammer_ = o.hammer_;
    mech_ = o.mech_;
    env_ = o.env_;
    was_held_ = o.was_held_;
    edge_ = o.edge_;
    declick_ = o.declick_;
    key_ = o.key_;
    n_ = o.n_;
    damp_ = o.damp_;
    gamma_ = o.gamma_;
    out_scale_ = o.out_scale_;
    active_ = o.active_;
    quiet_ = o.quiet_;
    last_ = o.last_;
  }

 private:
  void strike(const float* ins) {
    const float voct = clamp_nan(ins[1], -5.0f, 5.0f);
    const int key = std::clamp(static_cast<int>(std::lround(57.0f + 12.0f * voct)) - 21, 0, kKeys - 1);
    const KeyDesign& d = design_[key];
    if (active_ && key != key_) {  // a stolen voice: cut the old note cleanly
      declick_.jump(last_, 0.0f, 0.004f * fs_);
      silence();
    }
    const float stretch = clamp_nan(ins[6], 0.0f, 2.0f) * piano::stretch_cents(key);
    const double f = 220.0 * std::exp2(voct + stretch / 1200.0);
    const float detune = clamp_nan(ins[4], 0.0f, 8.0f);
    const float decay = clamp_nan(ins[5], 0.05f, 8.0f);
    for (int i = 0; i < d.strings; ++i) {
      const double fi = f * std::exp2(detune * d.detune[i] / 1200.0);
      str_[i].tune(d, std::clamp(fi, 20.0, 0.2 * fs_), fs_, max_len_);
      str_[i].loss_g = std::pow(d.loss_g, 1.0f / decay);
    }
    const float vel = clamp_nan(ins[2], 0.0f, 1.0f);
    const float hard = std::exp2(4.0f * (clamp_nan(ins[3], 0.0f, 1.0f) - 0.5f));
    hammer_.strike(d, 0.25f * std::pow(24.0f, vel), hard, fs_);
    const float treble = static_cast<float>(key) / (kKeys - 1);
    mech_.strike(vel, treble, clamp_nan(ins[8], 0.0f, 4.0f), fs_);
    key_ = key;
    n_ = d.strings;
    gamma_ = d.gamma;
    out_scale_ = 2.0f * d.z * d.gain * kOutGain;
    active_ = true;
    quiet_ = 0;
  }

  float tick(bool held, float pedal) {
    const KeyDesign& d = design_[key_];
    // Half-pedal: the dampers touch lightly, partway between lifted and down.
    const float lift = held ? 1.0f : clamp_nan(pedal, 0.0f, 1.0f);
    const float target = d.damp + lift * (1.0f - d.damp);
    damp_ += (target - damp_) * damp_rate_;
    std::array<float, piano::kMaxStrings> va{}, vb{};
    float mean = 0.0f, out = 0.0f;
    for (int i = 0; i < n_; ++i) {
      va[i] = str_[i].from_agraffe();
      vb[i] = str_[i].at_bridge();
      mean += vb[i];
      out += str_[i].bridge_tap();
    }
    mean /= static_cast<float>(n_);
    float v0 = 0.0f;
    for (int i = 0; i < n_; ++i) {
      vb[i] = str_[i].terminate(vb[i] - gamma_ * mean, damp_);
      v0 += va[i] + vb[i];
    }
    const float f = hammer_.step(v0 / static_cast<float>(n_), 1.0f / fs_);
    const float inj = f / hammer_.z2n;
    for (int i = 0; i < n_; ++i) str_[i].push(va[i] + inj, vb[i] + inj);
    return out * out_scale_;
  }

  // Key up: the damper lands (unless the pedal or the top octaves keep it off).
  void release(const float* ins) {
    const float lifted = clamp_nan(ins[7], 0.0f, 1.0f);
    if (key_ >= piano::kFirstUndamped || lifted > 0.5f) return;
    mech_.release(env_ * (1.0f - lifted), clamp_nan(ins[8], 0.0f, 4.0f));
  }

  void silence() {
    for (auto& s : str_) s.clear();
    hammer_.life = 0;
    active_ = false;
    quiet_ = 0;
  }

  std::array<KeyDesign, kKeys> design_{};
  std::array<piano::String, piano::kMaxStrings> str_{};
  piano::Hammer hammer_;
  piano::Mechanics mech_;
  RisingEdge edge_;
  Declick declick_;
  float fs_ = 48000.0f, damp_rate_ = 0.01f, damp_ = 1.0f, gamma_ = 0.0f;
  float out_scale_ = 0.0f, last_ = 0.0f, env_ = 0.0f, env_fall_ = 0.999f;
  bool was_held_ = false;
  int max_len_ = 2400, key_ = 39, n_ = 1, quiet_ = 0;
  bool active_ = false;
};

std::unique_ptr<Module> make() { return std::make_unique<PianoModule>(); }
}  // namespace
const ModuleInfo piano_info{"piano", kIns, kCfgs, kOuts, &make};
}  // namespace ms::dsp
