#include <algorithm>
#include <cmath>
#include <cstdint>
#include "dsp/modules.h"

// midi: pure event -> CV with real note identity. No I/O — events arrive via
// on_midi (raw channel-voice bytes), outputs are computed in process() from
// tracker state. Design: docs/superpowers/specs/2026-07-11-midi-design.md.
// Deliberately banks-only (no mono gate/voct/vel): those bare spellings are
// reserved by the voice-group bank-resolution rule (spec §1).
namespace ms::dsp {
namespace {
constexpr const char* kModes[] = {"last", "lowest", nullptr};
constexpr ConfigInfo kCfg[] = {{"channel", nullptr}, {"voices", nullptr, ConfigKind::Number, 1.0},
                               {"mode", kModes},       {"bend", nullptr},
                               {"cc", nullptr, ConfigKind::Numbers}};
constexpr OutputInfo kOuts[] = {
    {"gate1"}, {"gate2"}, {"gate3"}, {"gate4"},
    {"gate5"}, {"gate6"}, {"gate7"}, {"gate8"},
    {"voct1"}, {"voct2"}, {"voct3"}, {"voct4"},
    {"voct5"}, {"voct6"}, {"voct7"}, {"voct8"},
    {"vel1"},  {"vel2"},  {"vel3"},  {"vel4"},
    {"vel5"},  {"vel6"},  {"vel7"},  {"vel8"},
    {"bend"},  {"at"},
    {"cc1"},   {"cc2"},   {"cc3"},   {"cc4"}};

constexpr int kMaxVoices = 8;
constexpr int kStackDepth = 16;  // mono held-key stack
constexpr int kCcSlots = 4;

class Midi final : public Module {
 public:
  void init(double sample_rate, const Config& cfg) override {
    voices_ = static_cast<int>(std::clamp(cfg_num(cfg, "voices", kCfg[1].def), 1.0, 8.0));
    channel_ = static_cast<int>(std::clamp(cfg_num(cfg, "channel", 0.0), 0.0, 16.0));
    bend_semis_ = static_cast<float>(std::clamp(cfg_num(cfg, "bend", 2.0), 0.0, 24.0));
    lowest_ = cfg_str(cfg, "mode", "last") == "lowest";
    const auto& cc = cfg_arr(cfg, "cc");
    for (int k = 0; k < kCcSlots; ++k) {
      if (k < static_cast<int>(cc.size()))
        cc_ctrl_[k] = static_cast<int>(std::clamp(cc[static_cast<size_t>(k)], 0.0, 127.0));
      else
        cc_ctrl_[k] = -1;  // unmapped
    }
    // ~3 ms one-pole smoothing on cc slots (spec §1).
    const double sr = sample_rate > 0.0 ? sample_rate : 48000.0;
    cc_coeff_ = static_cast<float>(1.0 - std::exp(-1.0 / (0.003 * sr)));
  }

  void process(const float* /*ins*/, float* outs) override {
    const float bend_voct = bend_norm_ * bend_semis_ / 12.f;
    for (int i = 0; i < kMaxVoices; ++i) {
      const bool active = i < voices_;
      const bool gate = active && gate_[i] && !gap_[i];
      outs[i] = gate ? 1.f : 0.f;
      outs[8 + i] = active ? voct_[i] + bend_voct : 0.f;
      outs[16 + i] = active ? vel_[i] : 0.f;
      gap_[i] = false;  // one-sample gap consumed
    }
    outs[24] = bend_voct;
    outs[25] = at_;
    for (int k = 0; k < kCcSlots; ++k) {
      cc_val_[k] += (cc_target_[k] - cc_val_[k]) * cc_coeff_;
      outs[26 + k] = cc_val_[k];
    }
  }

  void on_midi(const uint8_t* data, size_t len) override {
    if (len < 2) return;
    const uint8_t status = data[0];
    if (status < 0x80) return;  // not a status byte
    const int type = status & 0xF0;
    const int chan = status & 0x0F;
    // Channel filter: 0 = omni, else match 1-based channel config.
    if (channel_ != 0 && channel_ != chan + 1) return;

    switch (type) {
      case 0x90:  // note-on (vel 0 == note-off)
        if (len >= 3) {
          if (data[2] == 0) note_off(data[1]);
          else note_on(data[1], data[2]);
        }
        break;
      case 0x80:  // note-off
        note_off(data[1]);
        break;
      case 0xB0:  // control change
        if (len >= 3) control_change(data[1], data[2]);
        break;
      case 0xE0:  // pitch bend
        if (len >= 3) {
          const int value14 = static_cast<int>(data[1]) | (static_cast<int>(data[2]) << 7);
          bend_norm_ = static_cast<float>(value14 - 8192) / 8192.f;
        }
        break;
      case 0xD0:  // channel pressure
        at_ = static_cast<float>(data[1]) / 127.f;
        break;
      default:
        break;
    }
  }

  void copy_state_from(const Module& other) override {
    const auto& o = static_cast<const Midi&>(other);
    for (int i = 0; i < kMaxVoices; ++i) {
      gate_[i] = o.gate_[i];
      gap_[i] = o.gap_[i];
      sustained_[i] = o.sustained_[i];
      note_[i] = o.note_[i];
      order_[i] = o.order_[i];
      voct_[i] = o.voct_[i];
      vel_[i] = o.vel_[i];
    }
    stack_n_ = o.stack_n_;
    for (int i = 0; i < kStackDepth; ++i) stack_[i] = o.stack_[i];
    order_counter_ = o.order_counter_;
    rr_ = o.rr_;
    pedal_ = o.pedal_;
    bend_norm_ = o.bend_norm_;
    at_ = o.at_;
    for (int k = 0; k < kCcSlots; ++k) {
      cc_target_[k] = o.cc_target_[k];
      cc_val_[k] = o.cc_val_[k];
    }
  }

 private:
  static float note_voct(int note) { return static_cast<float>(note - 57) / 12.f; }
  static float vel_norm(int vel) { return static_cast<float>(vel) / 127.f; }

  void note_on(uint8_t note, uint8_t vel) {
    if (voices_ == 1) {
      mono_press(note, vel);
      return;
    }
    // Re-strike of a note already sounding or sustained: reclaim its own
    // voice rather than claiming another one, so a pedal-sustained note
    // struck again doesn't strand a second voice once the pedal lifts.
    int claim = -1;
    for (int i = 0; i < voices_; ++i) {
      if (gate_[i] && note_[i] == note) { claim = i; break; }
    }
    if (claim < 0) {
      // Poly: claim a free voice round-robin; if none free, steal the oldest.
      for (int s = 1; s <= voices_; ++s) {
        const int i = (rr_ + s) % voices_;
        if (!gate_[i]) { claim = i; break; }
      }
    }
    if (claim < 0) {
      // No free voice: steal the oldest sounding one (smallest order_).
      claim = 0;
      for (int i = 1; i < voices_; ++i)
        if (order_[i] < order_[claim]) claim = i;
    }
    rr_ = claim;
    // Stealing a still-sounding voice inserts a one-sample gate gap so the
    // downstream envelope retriggers (spec §2). Reclaiming an already-low
    // (released) voice needs no gap: the rising edge itself retriggers.
    gap_[claim] = gate_[claim];
    gate_[claim] = true;
    sustained_[claim] = false;
    note_[claim] = note;
    voct_[claim] = note_voct(note);
    vel_[claim] = vel_norm(vel);
    order_[claim] = ++order_counter_;
  }

  void note_off(uint8_t note) {
    if (voices_ == 1) {
      mono_release(note);
      return;
    }
    for (int i = 0; i < voices_; ++i) {
      if (gate_[i] && note_[i] == note) {
        if (pedal_) sustained_[i] = true;  // defer until pedal release
        else gate_[i] = false;             // voct/vel hold for the tail
        return;
      }
    }
  }

  void control_change(uint8_t num, uint8_t val) {
    if (num == 64) {  // sustain pedal
      const bool down = val >= 64;
      if (pedal_ && !down) {
        pedal_ = false;
        if (voices_ == 1) mono_pedal_up();
        else
          for (int i = 0; i < voices_; ++i)
            if (sustained_[i]) { gate_[i] = false; sustained_[i] = false; }
      } else {
        pedal_ = down;
      }
      return;
    }
    for (int k = 0; k < kCcSlots; ++k)
      if (cc_ctrl_[k] == static_cast<int>(num))
        cc_target_[k] = static_cast<float>(val) / 127.f;
  }

  // --- Mono (voices == 1): legato held-key stack, no retrigger gaps. -------
  struct Held { uint8_t note; float vel; bool released; };

  void stack_remove(uint8_t note) {
    for (int i = 0; i < stack_n_; ++i) {
      if (stack_[i].note == note) {
        for (int j = i; j < stack_n_ - 1; ++j) stack_[j] = stack_[j + 1];
        --stack_n_;
        return;
      }
    }
  }

  void mono_press(uint8_t note, uint8_t vel) {
    stack_remove(note);
    if (stack_n_ == kStackDepth) {  // drop the oldest to make room
      for (int j = 0; j < stack_n_ - 1; ++j) stack_[j] = stack_[j + 1];
      --stack_n_;
    }
    stack_[stack_n_++] = Held{note, vel_norm(vel), false};
    mono_update();
  }

  void mono_release(uint8_t note) {
    if (pedal_) {
      for (int i = 0; i < stack_n_; ++i)
        if (stack_[i].note == note) { stack_[i].released = true; break; }
    } else {
      stack_remove(note);
    }
    mono_update();
  }

  void mono_pedal_up() {  // drop every physically-released key
    int w = 0;
    for (int i = 0; i < stack_n_; ++i)
      if (!stack_[i].released) stack_[w++] = stack_[i];
    stack_n_ = w;
    mono_update();
  }

  void mono_update() {
    if (stack_n_ == 0) { gate_[0] = false; return; }  // voct/vel hold
    int sel = stack_n_ - 1;  // last: newest press wins
    if (lowest_) {
      sel = 0;
      for (int i = 1; i < stack_n_; ++i)
        if (stack_[i].note < stack_[sel].note) sel = i;
    }
    gate_[0] = true;  // legato: never gaps (restore does not retrigger)
    voct_[0] = note_voct(stack_[sel].note);
    vel_[0] = stack_[sel].vel;
  }

  int voices_ = 1;
  int channel_ = 0;         // 0 = omni, else 1..16
  bool lowest_ = false;     // mono priority mode
  float bend_semis_ = 2.f;
  float cc_coeff_ = 0.f;
  int cc_ctrl_[kCcSlots] = {-1, -1, -1, -1};

  // Per-voice tracker (poly; index 0 also used by mono).
  bool gate_[kMaxVoices] = {};
  bool gap_[kMaxVoices] = {};
  bool sustained_[kMaxVoices] = {};
  uint8_t note_[kMaxVoices] = {};
  uint32_t order_[kMaxVoices] = {};
  float voct_[kMaxVoices] = {};
  float vel_[kMaxVoices] = {};
  uint32_t order_counter_ = 0;
  int rr_ = -1;

  // Mono held-key stack.
  Held stack_[kStackDepth] = {};
  int stack_n_ = 0;

  bool pedal_ = false;
  float bend_norm_ = 0.f;   // -1..~1 (pitch bend, pre-range scale)
  float at_ = 0.f;
  float cc_target_[kCcSlots] = {};
  float cc_val_[kCcSlots] = {};
};

std::unique_ptr<Module> make() { return std::make_unique<Midi>(); }
}  // namespace
const ModuleInfo midi_info{"midi", {}, kCfg, kOuts, &make, /*wants_midi=*/true};
}  // namespace ms::dsp
