#include <algorithm>
#include <cmath>
#include "dsp/modules.h"
#include "dsp/util.h"
#include "dsp/voice_bank.h"

namespace ms::dsp {
namespace {
constexpr InputInfo kIns[] = {{"gate", 0.f}, {"voct", 0.f}, {"vel", 1.f}};
constexpr const char* kModes[] = {"rr", "unison", nullptr};
constexpr ConfigInfo kCfg[] = {{"n", nullptr, ConfigKind::Number, 4.0}, {"mode", kModes}};

// Voice allocator: spreads a mono gate/voct/vel stream across up to sixteen
// indexed output banks. `rr` claims the next voice round-robin on each
// rising edge; `unison` claims every active voice on every edge. See
// docs/superpowers/specs/2026-07-11-voice-alloc-design.md §2 for the
// claim/track/hold semantics this implements.
class Alloc final : public Module {
 public:
  void init(double, const Config& cfg) override {
    n_ = static_cast<int>(std::clamp(cfg_num(cfg, "n", kCfg[0].def), 1.0, static_cast<double>(kBankWidth)));
    unison_ = cfg_str(cfg, "mode", "rr") == "unison";
  }
  void process(const float* ins, float* outs) override {
    const bool high = ins[0] > 0.5f;
    if (gate_edge_(ins[0])) claim(ins[2]);
    for (int i = 0; i < kBankWidth; ++i) {
      const bool active = i < n_;
      const bool newest = active && (unison_ || i == newest_);
      if (newest && high) voct_[i] = ins[1];  // track while newest
      // Follows input while it is the current claim; superseded or inactive
      // voices drop immediately.
      gate_[i] = newest && high;
      outs[i] = as_gate(active && gate_[i] && !gap_[i]);
      outs[kBankWidth + i] = active ? voct_[i] : 0.f;
      outs[2 * kBankWidth + i] = active ? vel_[i] : 0.f;
      outs[3 * kBankWidth + i] = active ? voice_spread(i, n_) : 0.f;
      gap_[i] = false;
    }
  }
  void copy_state_from(const Module& other) override {
    const auto& o = static_cast<const Alloc&>(other);
    newest_ = o.newest_;
    gate_edge_ = o.gate_edge_;
    for (int i = 0; i < kBankWidth; ++i) {
      gate_[i] = o.gate_[i];
      gap_[i] = o.gap_[i];
      voct_[i] = o.voct_[i];
      vel_[i] = o.vel_[i];
    }
  }

 private:
  // rr: claim the next voice round-robin. unison: claim all voices.
  void claim(float vel) {
    newest_ = unison_ ? -1 : (newest_ + 1) % n_;
    for (int i = 0; i < n_; ++i) {
      if (!unison_ && i != newest_) continue;
      // A reclaimed voice whose gate was still high (should the
      // round-robin ever wrap onto an unreleased voice) gets a
      // one-sample low gap so downstream envelopes retrigger.
      gap_[i] = gate_[i];
      gate_[i] = true;
      vel_[i] = vel;
    }
  }
  int n_ = 4;
  int newest_ = -1;
  bool unison_ = false;
  bool gate_[kBankWidth] = {};
  bool gap_[kBankWidth] = {};
  float voct_[kBankWidth] = {};
  float vel_[kBankWidth] = {};
  RisingEdge gate_edge_;
};
std::unique_ptr<Module> make() { return std::make_unique<Alloc>(); }
}  // namespace
const ModuleInfo alloc_info{"alloc", kIns, kCfg, kVoiceBankOutputs, &make};
}  // namespace ms::dsp
