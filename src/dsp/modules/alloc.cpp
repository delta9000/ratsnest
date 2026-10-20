#include <algorithm>
#include <cmath>
#include "dsp/modules.h"
#include "dsp/util.h"

namespace ms::dsp {
namespace {
constexpr InputInfo kIns[] = {{"gate", 0.f}, {"voct", 0.f}, {"vel", 1.f}};
constexpr const char* kModes[] = {"rr", "unison", nullptr};
constexpr ConfigInfo kCfg[] = {{"n", nullptr}, {"mode", kModes}};
constexpr OutputInfo kOuts[] = {
    {"gate1"}, {"gate2"}, {"gate3"}, {"gate4"},
    {"gate5"}, {"gate6"}, {"gate7"}, {"gate8"},
    {"voct1"}, {"voct2"}, {"voct3"}, {"voct4"},
    {"voct5"}, {"voct6"}, {"voct7"}, {"voct8"},
    {"vel1"}, {"vel2"}, {"vel3"}, {"vel4"},
    {"vel5"}, {"vel6"}, {"vel7"}, {"vel8"},
    {"spread1"}, {"spread2"}, {"spread3"}, {"spread4"},
    {"spread5"}, {"spread6"}, {"spread7"}, {"spread8"}};

// Voice allocator: spreads a mono gate/voct/vel stream across up to eight
// indexed output banks. `rr` claims the next voice round-robin on each
// rising edge; `unison` claims every active voice on every edge. See
// docs/superpowers/specs/2026-07-11-voice-alloc-design.md §2 for the
// claim/track/hold semantics this implements.
class Alloc final : public Module {
 public:
  void init(double, const Config& cfg) override {
    n_ = static_cast<int>(std::clamp(cfg_num(cfg, "n", 4.0), 1.0, 8.0));
    unison_ = cfg_str(cfg, "mode", "rr") == "unison";
  }
  void process(const float* ins, float* outs) override {
    const bool high = ins[0] > 0.5f;
    if (gate_edge_(ins[0])) claim(ins[2]);
    for (int i = 0; i < 8; ++i) {
      const bool active = i < n_;
      const bool newest = active && (unison_ || i == newest_);
      if (newest && high) voct_[i] = ins[1];  // track while newest
      // Follows input while it is the current claim; superseded or inactive
      // voices drop immediately.
      gate_[i] = newest && high;
      outs[i] = as_gate(active && gate_[i] && !gap_[i]);
      outs[8 + i] = active ? voct_[i] : 0.f;
      outs[16 + i] = active ? vel_[i] : 0.f;
      outs[24 + i] = active ? spread(i) : 0.f;
      gap_[i] = false;
    }
  }
  void copy_state_from(const Module& other) override {
    const auto& o = static_cast<const Alloc&>(other);
    newest_ = o.newest_;
    gate_edge_ = o.gate_edge_;
    for (int i = 0; i < 8; ++i) {
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
  // Stereo position of voice i, spread evenly over -1..1.
  float spread(int i) const {
    if (n_ == 1) return 0.f;
    return -1.f + 2.f * static_cast<float>(i) / static_cast<float>(n_ - 1);
  }

  int n_ = 4;
  int newest_ = -1;
  bool unison_ = false;
  bool gate_[8] = {};
  bool gap_[8] = {};
  float voct_[8] = {};
  float vel_[8] = {};
  RisingEdge gate_edge_;
};
std::unique_ptr<Module> make() { return std::make_unique<Alloc>(); }
}  // namespace
const ModuleInfo alloc_info{"alloc", kIns, kCfg, kOuts, &make};
}  // namespace ms::dsp
