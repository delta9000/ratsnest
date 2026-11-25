// A tiny CLAP plugin for testing the `plugin` module: out = in * Gain, plus,
// while a note is held, its velocity on the left and key/127 on the right
// (the last note on; releasing an older key changes nothing).
// Deterministic DC so tests can check exact timing and values.
#include <clap/clap.h>

#include <cstring>

namespace {

constexpr clap_id kGainId = 3;

struct Tone {
  clap_plugin_t plugin;
  double gain = 1.0;
  float vel = 0.0f, key = 0.0f;
  bool active = false;
};

Tone* self(const clap_plugin_t* p) { return static_cast<Tone*>(p->plugin_data); }

void on_event(Tone& t, const clap_event_header_t* h) {
  if (h->space_id != CLAP_CORE_EVENT_SPACE_ID) return;
  if (h->type == CLAP_EVENT_PARAM_VALUE) {
    const auto* e = reinterpret_cast<const clap_event_param_value_t*>(h);
    if (e->param_id == kGainId) t.gain = e->value;
  } else if (h->type == CLAP_EVENT_NOTE_ON) {
    const auto* e = reinterpret_cast<const clap_event_note_t*>(h);
    t.vel = static_cast<float>(e->velocity);
    t.key = static_cast<float>(e->key) / 127.0f;
  } else if (h->type == CLAP_EVENT_NOTE_OFF) {
    // Only the held key's release silences it, as in a mono synth.
    const auto* e = reinterpret_cast<const clap_event_note_t*>(h);
    if (static_cast<float>(e->key) / 127.0f == t.key) t.vel = t.key = 0.0f;
  } else if (h->type == CLAP_EVENT_MIDI) {
    const auto* e = reinterpret_cast<const clap_event_midi_t*>(h);
    const bool on = (e->data[0] & 0xf0) == 0x90 && e->data[2] > 0;
    t.vel = on ? static_cast<float>(e->data[2]) / 127.0f : 0.0f;
    t.key = on ? static_cast<float>(e->data[1]) / 127.0f : 0.0f;
  }
}

clap_process_status process(const clap_plugin_t* p, const clap_process_t* pr) {
  Tone& t = *self(p);
  const uint32_t n_ev = pr->in_events->size(pr->in_events);
  uint32_t next = 0;
  for (uint32_t i = 0; i < pr->frames_count; ++i) {
    while (next < n_ev) {
      const clap_event_header_t* h = pr->in_events->get(pr->in_events, next);
      if (h->time > i) break;
      on_event(t, h);
      ++next;
    }
    for (uint32_t c = 0; c < 2; ++c) {
      const float in = pr->audio_inputs_count ? pr->audio_inputs[0].data32[c][i] : 0.0f;
      pr->audio_outputs[0].data32[c][i] =
          in * static_cast<float>(t.gain) + (c == 0 ? t.vel : t.key);
    }
  }
  return CLAP_PROCESS_CONTINUE;
}

uint32_t ports_count(const clap_plugin_t*, bool) { return 1; }
bool ports_get(const clap_plugin_t*, uint32_t i, bool in, clap_audio_port_info_t* info) {
  if (i != 0) return false;
  std::memset(info, 0, sizeof(*info));
  std::strcpy(info->name, in ? "in" : "out");
  info->flags = CLAP_AUDIO_PORT_IS_MAIN;
  info->channel_count = 2;
  info->port_type = CLAP_PORT_STEREO;
  info->in_place_pair = CLAP_INVALID_ID;
  return true;
}
const clap_plugin_audio_ports_t kPorts{ports_count, ports_get};

uint32_t params_count(const clap_plugin_t*) { return 1; }
bool params_info(const clap_plugin_t*, uint32_t i, clap_param_info_t* info) {
  if (i != 0) return false;
  std::memset(info, 0, sizeof(*info));
  info->id = kGainId;
  std::strcpy(info->name, "Gain");
  std::strcpy(info->module, "Out");
  info->min_value = 0.0;
  info->max_value = 2.0;
  info->default_value = 1.0;
  info->flags = CLAP_PARAM_IS_AUTOMATABLE;
  return true;
}
bool params_value(const clap_plugin_t* p, clap_id id, double* v) {
  if (id != kGainId) return false;
  *v = self(p)->gain;
  return true;
}
bool params_text(const clap_plugin_t*, clap_id, double, char*, uint32_t) { return false; }
// Display text: "unity" parses as 1; anything else is left to the host.
bool params_parse(const clap_plugin_t*, clap_id id, const char* s, double* v) {
  if (id != kGainId || std::strcmp(s, "unity") != 0) return false;
  *v = 1.0;
  return true;
}
void params_flush(const clap_plugin_t* p, const clap_input_events_t* in, const clap_output_events_t*) {
  for (uint32_t i = 0; i < in->size(in); ++i) on_event(*self(p), in->get(in, i));
}
const clap_plugin_params_t kParams{params_count, params_info, params_value, params_text,
                                   params_parse, params_flush};

const void* extension(const clap_plugin_t*, const char* id) {
  if (!std::strcmp(id, CLAP_EXT_AUDIO_PORTS)) return &kPorts;
  if (!std::strcmp(id, CLAP_EXT_PARAMS)) return &kParams;
  return nullptr;
}

bool init(const clap_plugin_t*) { return true; }
void destroy(const clap_plugin_t* p) { delete self(p); }
bool activate(const clap_plugin_t* p, double, uint32_t, uint32_t) { return self(p)->active = true; }
void deactivate(const clap_plugin_t* p) { self(p)->active = false; }
bool start(const clap_plugin_t*) { return true; }
void stop(const clap_plugin_t*) {}
void reset(const clap_plugin_t*) {}
void main_thread(const clap_plugin_t*) {}

const char* const kFeatures[] = {CLAP_PLUGIN_FEATURE_INSTRUMENT, nullptr};
const clap_plugin_descriptor_t kDesc{CLAP_VERSION_INIT, "org.ratsnest.test.tone", "Test Tone",
                                     "ratsnest", "", "", "", "1.0", "", kFeatures};

uint32_t f_count(const clap_plugin_factory_t*) { return 1; }
const clap_plugin_descriptor_t* f_desc(const clap_plugin_factory_t*, uint32_t i) {
  return i == 0 ? &kDesc : nullptr;
}
const clap_plugin_t* f_create(const clap_plugin_factory_t*, const clap_host_t*, const char* id) {
  if (std::strcmp(id, kDesc.id) != 0) return nullptr;
  auto* t = new Tone;
  t->plugin = {&kDesc, t, init, destroy, activate, deactivate, start, stop, reset,
               process, extension, main_thread};
  return &t->plugin;
}
const clap_plugin_factory_t kFactory{f_count, f_desc, f_create};

bool e_init(const char*) { return true; }
void e_deinit() {}
const void* e_factory(const char* id) {
  return std::strcmp(id, CLAP_PLUGIN_FACTORY_ID) == 0 ? &kFactory : nullptr;
}

}  // namespace

extern "C" CLAP_EXPORT const clap_plugin_entry_t clap_entry{CLAP_VERSION_INIT, e_init, e_deinit,
                                                            e_factory};
