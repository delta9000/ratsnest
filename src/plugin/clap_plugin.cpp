// ratsnest as a CLAP plugin: every .patch in the plugin folder shows up in the
// host's plugin list as its own instrument (or audio effect, when the patch
// reads `extin`). The patch file stays the source of truth: the plugin keeps
// no state of its own, and saving the file hot-reloads it inside the host.
//
// The host's audio thread drives the same RtRunner `ratsnest play` uses; the
// host's main thread owns the Controller (load, reload, reaping), exactly as
// the play loop in src/main.cpp does. Host notes and raw MIDI become MIDI
// events stamped at their frame, so they land sample-accurately.
//
// CLAP is an MIT-licensed, header-only C ABI (github.com/free-audio/clap);
// only its headers are used. See docs/plugin.md.
#include <clap/clap.h>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <memory>
#include <string>
#include <vector>

#include "engine/controller.h"
#include "engine/rt_runner.h"
#include "graph/subs.h"
#include "io/watcher.h"

namespace {

namespace fs = std::filesystem;
using ms::engine::Controller;
using ms::engine::RtRunner;

constexpr uint32_t kReloadPollMs = 200;

// ---- discovery --------------------------------------------------------------

struct Desc {
  std::string id, name, path, description;
  bool effect = false;  // reads extin: declare an audio input
  std::vector<const char*> features;
  clap_plugin_descriptor_t clap{};
};
std::vector<std::unique_ptr<Desc>> g_descs;

fs::path plugin_dir() {
  if (const char* d = std::getenv("RATSNEST_PLUGIN_DIR"); d && *d) return d;
  if (const char* x = std::getenv("XDG_CONFIG_HOME"); x && *x)
    return fs::path(x) / "ratsnest" / "plugins";
  const char* home = std::getenv("HOME");
  return fs::path(home ? home : ".") / ".config" / "ratsnest" / "plugins";
}

// Symlinks resolve to their target so a patch's relative sub/trk/wav paths
// work when the folder links into a checkout.
std::vector<fs::path> find_patches() {
  std::vector<fs::path> found;
  std::error_code ec;
  for (const auto& e : fs::directory_iterator(plugin_dir(), ec)) {
    if (e.path().extension() != ".patch") continue;
    const fs::path real = fs::canonical(e.path(), ec);
    if (!ec && fs::is_regular_file(real)) found.push_back(e.path());
  }
  std::sort(found.begin(), found.end());
  return found;
}

std::unique_ptr<Desc> describe(const fs::path& p) {
  auto d = std::make_unique<Desc>();
  const std::string stem = p.stem().string();
  d->id = "org.ratsnest.patch." + stem;
  d->name = stem;
  d->path = fs::canonical(p).string();
  std::string src;
  if (ms::graph::read_text_file(d->path, src)) {
    const auto b = ms::graph::build_patch(src, d->path, 48000.0);
    if (b.compiled.ok())
      for (const auto& n : b.compiled.graph->nodes) d->effect |= n.info->wants_audio_in;
  }
  d->description = "ratsnest patch " + d->path;
  d->features = {d->effect ? CLAP_PLUGIN_FEATURE_AUDIO_EFFECT : CLAP_PLUGIN_FEATURE_INSTRUMENT,
                 d->effect ? CLAP_PLUGIN_FEATURE_MULTI_EFFECTS : CLAP_PLUGIN_FEATURE_SYNTHESIZER,
                 CLAP_PLUGIN_FEATURE_STEREO, nullptr};
  d->clap = {CLAP_VERSION_INIT, d->id.c_str(), d->name.c_str(), "ratsnest", "", "", "",
             "0.1.0", d->description.c_str(), d->features.data()};
  return d;
}

// ---- instance ---------------------------------------------------------------

struct Instance {
  clap_plugin_t plugin{};
  const clap_host_t* host = nullptr;
  const Desc* desc = nullptr;
  const clap_host_log_t* log = nullptr;
  const clap_host_timer_support_t* timers = nullptr;
  clap_id timer = CLAP_INVALID_ID;
  double rate = 48000.0;
  uint64_t clock_ns = 0;  // the runner's clock: frames processed, in ns
  std::unique_ptr<RtRunner> runner;
  std::unique_ptr<Controller> ctl;
  std::vector<std::string> watched;
  std::unique_ptr<ms::io::PatchWatcher> watcher;
};

Instance* self(const clap_plugin_t* p) { return static_cast<Instance*>(p->plugin_data); }

void say(const Instance& s, clap_log_severity sev, const std::string& msg) {
  if (s.log) s.log->log(s.host, sev, msg.c_str());
  else std::fprintf(stderr, "ratsnest: %s\n", msg.c_str());
}

// Same line:col form as `ratsnest check`; the last good graph keeps playing.
// A good load re-aims the watcher at the patch plus every file it pulled in.
bool report(Instance& s, const Controller::LoadResult& res) {
  for (const auto& d : res.errors)
    say(s, CLAP_LOG_ERROR, (d.file.empty() ? s.desc->path : d.file) + ":" +
        std::to_string(d.line) + ":" + std::to_string(d.col) + ": error: " + d.msg);
  if (!res.ok || res.deferred) return false;  // deferred: a later on_timer applies it
  std::vector<std::string> want{s.desc->path};
  want.insert(want.end(), res.files.begin(), res.files.end());
  if (!s.watcher || want != s.watched) {
    s.watched = std::move(want);
    s.watcher = std::make_unique<ms::io::PatchWatcher>(s.watched);
  }
  return true;
}

bool load(Instance& s) {
  std::string src;
  if (!ms::graph::read_text_file(s.desc->path, src)) {
    say(s, CLAP_LOG_ERROR, "cannot read " + s.desc->path);
    return false;
  }
  return report(s, s.ctl->load(src, s.desc->path));
}

bool p_init(const clap_plugin_t* p) {
  Instance& s = *self(p);
  s.log = static_cast<const clap_host_log_t*>(s.host->get_extension(s.host, CLAP_EXT_LOG));
  s.timers = static_cast<const clap_host_timer_support_t*>(
      s.host->get_extension(s.host, CLAP_EXT_TIMER_SUPPORT));
  if (s.timers && !s.timers->register_timer(s.host, kReloadPollMs, &s.timer))
    s.timers = nullptr;
  if (!s.timers) say(s, CLAP_LOG_WARNING, "host has no timer support; hot-reload disabled");
  return true;
}

void p_destroy(const clap_plugin_t* p) {
  Instance* s = self(p);
  if (s->timers) s->timers->unregister_timer(s->host, s->timer);
  delete s;
}

bool p_activate(const clap_plugin_t* p, double rate, uint32_t, uint32_t) {
  Instance& s = *self(p);
  s.rate = rate;
  s.clock_ns = 0;
  s.runner = std::make_unique<RtRunner>(rate);
  s.ctl = std::make_unique<Controller>(*s.runner, rate);
  load(s);  // a bad patch still activates: silence until a good save
  return true;
}

void p_deactivate(const clap_plugin_t* p) {
  Instance& s = *self(p);
  s.ctl.reset();
  s.runner.reset();  // the audio thread is stopped: deletes in-flight engines
}

bool p_start(const clap_plugin_t*) { return true; }
void p_stop(const clap_plugin_t*) {}
void p_reset(const clap_plugin_t*) {}

// ---- audio thread -----------------------------------------------------------

uint8_t midi_channel(int16_t ch) { return static_cast<uint8_t>(ch < 0 ? 0 : ch & 0x0f); }

// Host note events and raw MIDI as the 1..3-byte MIDI the `midi` module eats.
// A wildcard note-off (key -1) is All Notes Off.
bool to_midi(const clap_event_header_t* h, ms::dsp::MidiEvent& m) {
  if (h->space_id != CLAP_CORE_EVENT_SPACE_ID) return false;
  if (h->type == CLAP_EVENT_MIDI) {
    const auto* e = reinterpret_cast<const clap_event_midi_t*>(h);
    const uint8_t kind = e->data[0] & 0xf0;
    m = {{e->data[0], e->data[1], e->data[2], 0},
         static_cast<uint8_t>(kind == 0xc0 || kind == 0xd0 ? 2 : 3)};
    return kind >= 0x80 && kind < 0xf0;
  }
  if (h->type != CLAP_EVENT_NOTE_ON && h->type != CLAP_EVENT_NOTE_OFF &&
      h->type != CLAP_EVENT_NOTE_CHOKE)
    return false;
  const auto* e = reinterpret_cast<const clap_event_note_t*>(h);
  const uint8_t ch = midi_channel(e->channel);
  if (e->key < 0) {
    m = {{static_cast<uint8_t>(0xb0 | ch), 123, 0, 0}, 3};
    return h->type != CLAP_EVENT_NOTE_ON;
  }
  const auto key = static_cast<uint8_t>(e->key & 0x7f);
  if (h->type == CLAP_EVENT_NOTE_ON) {
    const auto vel = static_cast<uint8_t>(std::clamp(std::lround(e->velocity * 127.0), 1L, 127L));
    m = {{static_cast<uint8_t>(0x90 | ch), key, vel, 0}, 3};
  } else {
    m = {{static_cast<uint8_t>(0x80 | ch), key, 64, 0}, 3};
  }
  return true;
}

uint64_t frames_ns(const Instance& s, uint32_t frames) {
  return static_cast<uint64_t>(std::llround(static_cast<double>(frames) * 1e9 / s.rate));
}

void push_events(Instance& s, const clap_input_events_t* in) {
  ms::dsp::MidiEvent m{};
  for (uint32_t i = 0, n = in->size(in); i < n; ++i) {
    const clap_event_header_t* h = in->get(in, i);
    if (to_midi(h, m)) s.runner->push_midi(m, s.clock_ns + frames_ns(s, h->time));
  }
}

// The input bus, mixed to mono, for `extin`. Pushed ahead of render, so it is
// heard in the same block (no added latency).
void push_audio(Instance& s, const clap_audio_buffer_t& in, uint32_t frames) {
  if (in.channel_count == 0 || !in.data32) return;
  const float* l = in.data32[0];
  const float* r = in.data32[std::min<uint32_t>(1, in.channel_count - 1)];
  ms::engine::AudioChunk c{};
  for (uint32_t done = 0; done < frames; done += c.len) {
    c.len = std::min<uint32_t>(64, frames - done);
    for (uint32_t i = 0; i < c.len; ++i) c.s[i] = 0.5f * (l[done + i] + r[done + i]);
    s.runner->push_audio(c);
  }
}

clap_process_status p_process(const clap_plugin_t* p, const clap_process_t* pr) {
  Instance& s = *self(p);
  if (pr->audio_outputs_count == 0 || pr->audio_outputs[0].channel_count == 0)
    return CLAP_PROCESS_ERROR;
  push_events(s, pr->in_events);
  if (pr->audio_inputs_count > 0) push_audio(s, pr->audio_inputs[0], pr->frames_count);
  float** out = pr->audio_outputs[0].data32;
  float* r = out[std::min<uint32_t>(1, pr->audio_outputs[0].channel_count - 1)];
  s.runner->render(out[0], r, pr->frames_count, s.clock_ns);
  s.clock_ns += frames_ns(s, pr->frames_count);
  return CLAP_PROCESS_CONTINUE;
}

// ---- extensions -------------------------------------------------------------

uint32_t audio_count(const clap_plugin_t* p, bool is_input) {
  return is_input ? (self(p)->desc->effect ? 1u : 0u) : 1u;
}

bool audio_get(const clap_plugin_t* p, uint32_t index, bool is_input,
               clap_audio_port_info_t* info) {
  if (index >= audio_count(p, is_input)) return false;
  *info = {};
  info->id = 0;
  std::snprintf(info->name, sizeof(info->name), "%s", is_input ? "in" : "out");
  info->flags = CLAP_AUDIO_PORT_IS_MAIN;
  info->channel_count = 2;
  info->port_type = CLAP_PORT_STEREO;
  info->in_place_pair = CLAP_INVALID_ID;
  return true;
}

uint32_t note_count(const clap_plugin_t*, bool is_input) { return is_input ? 1u : 0u; }

bool note_get(const clap_plugin_t*, uint32_t index, bool is_input, clap_note_port_info_t* info) {
  if (!is_input || index != 0) return false;
  *info = {};
  info->id = 0;
  info->supported_dialects = CLAP_NOTE_DIALECT_CLAP | CLAP_NOTE_DIALECT_MIDI;
  info->preferred_dialect = CLAP_NOTE_DIALECT_MIDI;
  std::snprintf(info->name, sizeof(info->name), "midi-in");
  return true;
}

// Main thread, every kReloadPollMs: the play loop's HotReload, host-driven.
void on_timer(const clap_plugin_t* p, clap_id) {
  Instance& s = *self(p);
  if (!s.ctl) return;
  if (s.watcher && s.watcher->poll_changed(0)) {
    if (load(s)) say(s, CLAP_LOG_INFO, "reloaded " + s.desc->path);
  } else if (auto res = s.ctl->apply_pending()) {
    report(s, *res);
  }
  s.ctl->drain_retired();
}

const clap_plugin_audio_ports_t kAudioPorts{audio_count, audio_get};
const clap_plugin_note_ports_t kNotePorts{note_count, note_get};
const clap_plugin_timer_support_t kTimer{on_timer};

const void* p_extension(const clap_plugin_t*, const char* id) {
  const std::string_view want(id);
  if (want == CLAP_EXT_AUDIO_PORTS) return &kAudioPorts;
  if (want == CLAP_EXT_NOTE_PORTS) return &kNotePorts;
  if (want == CLAP_EXT_TIMER_SUPPORT) return &kTimer;
  return nullptr;
}

void p_main_thread(const clap_plugin_t*) {}

// ---- factory and entry ------------------------------------------------------

uint32_t f_count(const clap_plugin_factory_t*) { return static_cast<uint32_t>(g_descs.size()); }

const clap_plugin_descriptor_t* f_desc(const clap_plugin_factory_t*, uint32_t i) {
  return i < g_descs.size() ? &g_descs[i]->clap : nullptr;
}

const clap_plugin_t* f_create(const clap_plugin_factory_t*, const clap_host_t* host,
                              const char* id) {
  if (!clap_version_is_compatible(host->clap_version)) return nullptr;
  for (const auto& d : g_descs) {
    if (d->id != id) continue;
    auto* s = new Instance;
    s->host = host;
    s->desc = d.get();
    s->plugin = {&d->clap, s, p_init, p_destroy, p_activate, p_deactivate, p_start,
                 p_stop, p_reset, p_process, p_extension, p_main_thread};
    return &s->plugin;
  }
  return nullptr;
}

const clap_plugin_factory_t kFactory{f_count, f_desc, f_create};

bool e_init(const char*) {
  g_descs.clear();
  for (const auto& p : find_patches()) g_descs.push_back(describe(p));
  return true;
}

void e_deinit() { g_descs.clear(); }

const void* e_factory(const char* id) {
  return std::string_view(id) == CLAP_PLUGIN_FACTORY_ID ? &kFactory : nullptr;
}

}  // namespace

extern "C" CLAP_EXPORT const clap_plugin_entry_t clap_entry{CLAP_VERSION_INIT, e_init, e_deinit,
                                                            e_factory};
