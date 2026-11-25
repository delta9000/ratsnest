// `plugin`: a third-party CLAP plugin as a module (docs/plugin.md). Headless:
// no plugin GUI and no saved plugin state, so the patch stays the source of
// truth. Listed params are driven from inputs p1..p8 (normalized 0..1), notes
// come from gate/voct/vel (mono) and, with `midi: on`, from the keyboard.
//
// The plugin works in blocks, the graph in samples: inputs collect for
// kBlock samples, the plugin runs, and its output plays during the next
// block, so the module adds kBlock samples of latency. Loading happens in
// init (non-RT). The plugin's own process() is outside ratsnest's control:
// a plugin that allocates or locks there bends rule 3, and one that crashes
// takes the session with it.
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>
#include <string>
#include <vector>

#include "dsp/clap_host.h"
#include "dsp/modules.h"

namespace ms::dsp {
namespace {

constexpr size_t kBlock = 64;
constexpr size_t kParams = 8;
constexpr size_t kMaxEvents = 256;

constexpr InputInfo kIns[] = {
    {"in_l", 0.0f}, {"in_r", 0.0f}, {"gate", 0.0f, false}, {"voct", 0.0f}, {"vel", 0.8f},
    {"p1", 0.0f}, {"p2", 0.0f}, {"p3", 0.0f}, {"p4", 0.0f},
    {"p5", 0.0f}, {"p6", 0.0f}, {"p7", 0.0f}, {"p8", 0.0f},
};
enum In { kInL, kInR, kGate, kVoct, kVel, kP1 };
constexpr const char* kOnOff[] = {"off", "on", nullptr};
constexpr ConfigInfo kCfg[] = {
    {"file", nullptr, ConfigKind::Text},    // "Surge XT" or a path to a .clap
    {"id", nullptr, ConfigKind::Text},      // which plugin in the file; "" = first
    {"params", nullptr, ConfigKind::Texts}, // names (or ids) driven by p1..p8
    {"set", nullptr, ConfigKind::Texts},    // "Name=value" fixed once at load
    {"midi", kOnOff},                       // forward keyboard MIDI
};
constexpr OutputInfo kOuts[] = {{"out_l"}, {"out_r"}};

// One event of any kind the module sends; `header.size` says which.
union Event {
  clap_event_header_t header;
  clap_event_note_t note;
  clap_event_midi_t midi;
  clap_event_param_value_t param;
};

// A plugin audio port's buffers: channel_count × kBlock floats.
struct Port {
  std::vector<std::vector<float>> chans;
  std::vector<float*> ptrs;
  clap_audio_buffer_t buf{};
};

class Plugin final : public Module {
 public:
  ~Plugin() override { unload(); }

  void init(double rate, const Config& cfg) override {
    unload();
    midi_ = cfg_str(cfg, "midi", "off") == "on";
    const std::string file = cfg_str(cfg, "file", "");
    if (file.empty()) return;  // a `plugin {}` with nothing loaded: silence
    std::string err = load(rate, file, cfg_str(cfg, "id", ""), cfg_str_arr(cfg, "params"),
                           cfg_str_arr(cfg, "set"));
    if (!err.empty()) {
      std::fprintf(stderr, "warning: plugin '%s': %s; it will be silent\n", file.c_str(), err.c_str());
      unload();
    }
  }

  void on_midi(const uint8_t* data, size_t len) override {
    if (!p_ || !midi_ || len == 0) return;
    Event& e = push(sizeof(clap_event_midi_t), CLAP_EVENT_MIDI, pos_);
    for (size_t i = 0; i < 3; ++i) e.midi.data[i] = i < len ? data[i] : 0;
    e.midi.port_index = 0;
  }

  void process(const float* ins, float* outs) override {
    if (!p_) {
      outs[0] = outs[1] = 0.0f;
      return;
    }
    for (size_t i = 0; i < n_params_; ++i)  // p-locks: a param's first move lands on its sample
      if (!moved_[i]) moved_[i] = param(i, ins[kP1 + i], pos_);
    notes(ins);
    in_l_[pos_] = ins[kInL];
    in_r_[pos_] = ins[kInR];
    outs[0] = out_l_[pos_];
    outs[1] = out_r_[pos_];
    if (++pos_ == kBlock) run(ins);
  }

 private:
  std::string load(double rate, const std::string& file, const std::string& id,
                   const std::vector<std::string>& names, const std::vector<std::string>& sets) {
    const std::string path = clap::resolve(file);
    if (path.empty()) return "not found (see `ratsnest plugins`)";
    std::string err;
    const clap_plugin_factory_t* f = clap::factory(path, err);
    if (!f) return err;
    std::string want = id;
    if (want.empty() && f->get_plugin_count(f) > 0) want = f->get_plugin_descriptor(f, 0)->id;
    p_ = f->create_plugin(f, clap::host(), want.c_str());
    if (!p_ || !p_->init(p_)) return "cannot create '" + want + "'";
    if (std::string bad = clap::apply(p_, sets); !bad.empty()) return bad;
    if (std::string bad = bind_params(clap::params(p_), names); !bad.empty()) return bad;
    ports(true);
    ports(false);
    if (!p_->activate(p_, rate, 1, kBlock)) return "cannot activate '" + want + "'";
    active_ = true;
    return {};
  }

  // Each listed name (or numeric id) to the param it drives.
  std::string bind_params(const std::vector<clap::Param>& all, const std::vector<std::string>& names) {
    n_params_ = std::min(names.size(), kParams);
    for (size_t i = 0; i < n_params_; ++i) {
      const clap::Param* q = clap::find(all, names[i]);
      if (!q) return "no param named '" + names[i] + "'";
      bound_[i] = *q;
      sent_[i] = -1.0;
      moved_[i] = false;
    }
    return {};
  }

  static void fill_param(Event& e, clap_id id, double v) {
    e.param.param_id = id;
    e.param.cookie = nullptr;
    e.param.note_id = -1;
    e.param.port_index = -1;
    e.param.channel = -1;
    e.param.key = -1;
    e.param.value = v;
  }

  // Buffers for every port; main in/out are the module's in_l/r and out_l/r.
  void ports(bool input) {
    const auto* ext = static_cast<const clap_plugin_audio_ports_t*>(
        p_->get_extension(p_, CLAP_EXT_AUDIO_PORTS));
    auto& list = input ? ins_ : outs_;
    for (uint32_t i = 0, n = ext ? ext->count(p_, input) : 0; i < n; ++i) {
      clap_audio_port_info_t info{};
      if (!ext->get(p_, i, input, &info)) continue;
      Port& port = list.emplace_back();
      port.chans.assign(std::max(info.channel_count, 1u), std::vector<float>(kBlock, 0.0f));
      for (auto& c : port.chans) port.ptrs.push_back(c.data());
      port.buf.data32 = port.ptrs.data();
      port.buf.channel_count = static_cast<uint32_t>(port.chans.size());
    }
    in_bufs_.clear();
    out_bufs_.clear();
    for (auto& port : ins_) in_bufs_.push_back(port.buf);
    for (auto& port : outs_) out_bufs_.push_back(port.buf);
  }

  // Events stay in time order: notes, MIDI and each param's first move in a
  // block at the sample they arrive on; later moves on the block's last sample. Full: the last slot is overwritten.
  Event& push(uint16_t size, uint16_t type, size_t time) {
    Event& e = events_[std::min(n_events_, kMaxEvents - 1)];
    n_events_ = std::min(n_events_ + 1, kMaxEvents);
    e.header = {size, static_cast<uint32_t>(time), CLAP_CORE_EVENT_SPACE_ID, type, 0};
    return e;
  }

  void note(uint16_t type, int16_t key, double vel) {
    Event& e = push(sizeof(clap_event_note_t), type, pos_);
    e.note.note_id = -1;
    e.note.port_index = 0;
    e.note.channel = 0;
    e.note.key = key;
    e.note.velocity = vel;
  }

  // Gate rising through 0.5 plays voct's note (0 = A3, MIDI 57); falling
  // releases the note it started. A new semitone while the gate is high is a
  // slur: the new note starts before the old one ends, which a mono/legato
  // plugin patch glides.
  void notes(const float* ins) {
    const bool high = ins[kGate] > 0.5f;
    if (!high) {
      if (gate_) note(CLAP_EVENT_NOTE_OFF, key_, 0.0);
      gate_ = false;
      return;
    }
    const float k = std::round(57.0f + 12.0f * (std::isfinite(ins[kVoct]) ? ins[kVoct] : 0.0f));
    const auto key = static_cast<int16_t>(std::clamp(k, 0.0f, 127.0f));
    if (gate_ && key == key_) return;
    note(CLAP_EVENT_NOTE_ON, key, std::clamp(static_cast<double>(ins[kVel]), 0.0, 1.0));
    if (gate_) note(CLAP_EVENT_NOTE_OFF, key_, 0.0);
    gate_ = true;
    key_ = key;
  }

  // Block boundary: params that moved again since their first move, then the plugin.
  void run(const float* ins) {
    for (size_t i = 0; i < n_params_; ++i) param(i, ins[kP1 + i], kBlock - 1);
    moved_.fill(false);
    if (!ins_.empty()) copy_in();
    if (!started_) started_ = p_->start_processing(p_);
    clap_input_events_t in{this, &Plugin::ev_size, &Plugin::ev_get};
    clap_output_events_t out{nullptr, &Plugin::ev_push};
    clap_process_t pr{};
    pr.steady_time = static_cast<int64_t>(frames_);
    pr.frames_count = kBlock;
    pr.audio_inputs = in_bufs_.data();
    pr.audio_inputs_count = static_cast<uint32_t>(in_bufs_.size());
    pr.audio_outputs = out_bufs_.data();
    pr.audio_outputs_count = static_cast<uint32_t>(out_bufs_.size());
    pr.in_events = &in;
    pr.out_events = &out;
    const bool ok = p_->process(p_, &pr) != CLAP_PROCESS_ERROR;
    copy_out(ok);
    n_events_ = 0;
    pos_ = 0;
    frames_ += kBlock;
  }

  bool param(size_t i, float v, size_t time) {
    const clap::Param& b = bound_[i];
    double x = b.min + std::clamp(static_cast<double>(std::isfinite(v) ? v : 0.0f), 0.0, 1.0) *
                           (b.max - b.min);
    if (b.stepped) x = std::round(x);
    if (x == sent_[i]) return false;
    sent_[i] = x;
    fill_param(push(sizeof(clap_event_param_value_t), CLAP_EVENT_PARAM_VALUE, time), b.id, x);
    return true;
  }

  void copy_in() {
    Port& m = ins_[0];
    std::copy(in_l_.begin(), in_l_.end(), m.chans[0].begin());
    std::copy(in_r_.begin(), in_r_.end(), m.chans[std::min<size_t>(1, m.chans.size() - 1)].begin());
  }

  void copy_out(bool ok) {
    if (outs_.empty() || !ok) {
      out_l_.fill(0.0f);
      out_r_.fill(0.0f);
      return;
    }
    const Port& m = outs_[0];
    std::copy(m.chans[0].begin(), m.chans[0].end(), out_l_.begin());
    const auto& r = m.chans[std::min<size_t>(1, m.chans.size() - 1)];
    std::copy(r.begin(), r.end(), out_r_.begin());
  }

  void unload() {
    if (p_) {
      if (started_) p_->stop_processing(p_);
      if (active_) p_->deactivate(p_);
      p_->destroy(p_);
    }
    p_ = nullptr;
    started_ = active_ = false;
    ins_.clear();
    outs_.clear();
    n_params_ = 0;
  }

  static uint32_t ev_size(const clap_input_events_t* l) {
    return static_cast<uint32_t>(static_cast<const Plugin*>(l->ctx)->n_events_);
  }
  static const clap_event_header_t* ev_get(const clap_input_events_t* l, uint32_t i) {
    return &static_cast<const Plugin*>(l->ctx)->events_[i].header;
  }
  static bool ev_push(const clap_output_events_t*, const clap_event_header_t*) { return true; }

  const clap_plugin_t* p_ = nullptr;
  bool active_ = false, started_ = false, midi_ = false, gate_ = false;
  int16_t key_ = 0;
  size_t pos_ = 0, n_events_ = 0, n_params_ = 0;
  uint64_t frames_ = 0;
  std::array<float, kBlock> in_l_{}, in_r_{}, out_l_{}, out_r_{};
  std::vector<Port> ins_, outs_;
  std::vector<clap_audio_buffer_t> in_bufs_, out_bufs_;
  std::array<Event, kMaxEvents> events_{};
  std::array<clap::Param, kParams> bound_{};
  std::array<double, kParams> sent_{};
  std::array<bool, kParams> moved_{};
};

std::unique_ptr<Module> make() { return std::make_unique<Plugin>(); }

}  // namespace

const ModuleInfo plugin_info{"plugin", kIns, kCfg, kOuts, &make,
                             /*wants_midi=*/true, /*wants_audio_in=*/false};

}  // namespace ms::dsp
