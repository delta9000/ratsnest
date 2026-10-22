#include <algorithm>
#include <cmath>
#include "dsp/modules.h"
#include "dsp/util.h"
#include "dsp/voice_bank.h"

namespace ms::dsp {
namespace {
constexpr InputInfo kIns[] = {{"gate", 0.f}, {"vel", 1.f}, {"strum", 0.f},
                              {"p1", 0.f},   {"p2", 0.f},  {"p3", 0.f}, {"p4", 0.f},
                              {"p5", 0.f},   {"p6", 0.f},  {"p7", 0.f}, {"p8", 0.f}};
constexpr ConfigInfo kCfg[] = {{"n", nullptr, ConfigKind::Number, 4.0}};
constexpr int kPitch = 3;              // index of p1
constexpr int kCountCap = 1 << 30;     // far past the longest strum (3.5 s)

// Chord-to-bank: one gate and up to eight pitches become the voice banks a
// `voice { notes: ... }` group stamps from, so a tracker chord track (one
// gate, `voct`/`cva`/`cvb`/`cvd`) plays one stamped voice per chord tone.
// Voice i sounds p<i+1> while `gate` is high. Each rising gate latches `vel`
// and restarts the strum clock: voice i's gate rises i x `strum` seconds
// late (negative `strum` = down-strum, the top voice first). Once a voice's
// gate opens it stays open until `gate` falls, so moving or flipping
// `strum` mid-hold only reschedules voices not yet sounding. A voice tracks
// its pitch while its gate is high (a slur moves it without a retrigger)
// and holds it while low, so release tails and not-yet-strummed voices keep
// their old pitch. A gate drop of one sample retriggers every voice.
class Chord final : public Module {
 public:
  void init(double rate, const Config& cfg) override {
    rate_ = static_cast<float>(rate);
    n_ = static_cast<int>(std::clamp(cfg_num(cfg, "n", kCfg[0].def), 1.0, 8.0));
  }
  void process(const float* ins, float* outs) override {
    const bool high = ins[0] > 0.5f;
    if (gate_edge_(ins[0])) {
      count_ = 0;
      vel_ = ins[1];
    }
    const float strum = std::isfinite(ins[2]) ? std::clamp(ins[2], -0.5f, 0.5f) : 0.f;
    const float step = std::fabs(strum) * rate_;  // samples between voices
    const auto elapsed = static_cast<float>(count_);
    for (int i = 0; i < 8; ++i) {
      const bool active = i < n_;
      const int rank = strum < 0.f ? n_ - 1 - i : i;
      on_[i] = active && high && (on_[i] || elapsed >= static_cast<float>(rank) * step);
      if (on_[i]) voct_[i] = ins[kPitch + i];
      outs[i] = as_gate(on_[i]);
      outs[8 + i] = active ? voct_[i] : 0.f;
      outs[16 + i] = active ? vel_ : 0.f;
      outs[24 + i] = active ? voice_spread(i, n_) : 0.f;
    }
    if (high && count_ < kCountCap) ++count_;
  }
  void copy_state_from(const Module& other) override {
    const auto& o = static_cast<const Chord&>(other);
    gate_edge_ = o.gate_edge_;
    count_ = o.count_;
    vel_ = o.vel_;
    std::copy(std::begin(o.voct_), std::end(o.voct_), std::begin(voct_));
    std::copy(std::begin(o.on_), std::end(o.on_), std::begin(on_));
  }

 private:
  float rate_ = 48000.f;
  int n_ = 4;
  int count_ = 0;  // samples since the gate rose
  float vel_ = 0.f;
  float voct_[8] = {};
  bool on_[8] = {};  // latched: open until the gate falls
  RisingEdge gate_edge_;
};
std::unique_ptr<Module> make() { return std::make_unique<Chord>(); }
}  // namespace
const ModuleInfo chord_info{"chord", kIns, kCfg, kVoiceBankOutputs, &make};
}  // namespace ms::dsp
