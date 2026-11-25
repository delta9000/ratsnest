// A minimal CLAP host for ratsnest.clap: dlopens the built plugin and drives
// it the way a DAW does (scan, instantiate, activate, process with note and
// audio input, hot-reload on the host's timer, survive a bad save).
//
// Usage: clap_host_test <path/to/ratsnest.clap> <scratch dir> [doctest args]
#define DOCTEST_CONFIG_IMPLEMENT
#include <doctest/doctest.h>
#include <clap/clap.h>
#include <dlfcn.h>

#include <chrono>
#include <cmath>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <string>
#include <thread>
#include <vector>

namespace {

namespace fs = std::filesystem;

std::string g_plugin_path;
fs::path g_dir;
const clap_plugin_entry_t* g_entry = nullptr;

void write_file(const fs::path& p, const std::string& text) {
  std::ofstream(p) << text;
}

// ---- host side --------------------------------------------------------------

struct Host {
  clap_host_t clap{};
  std::vector<std::string> logs;
  clap_id timer = CLAP_INVALID_ID;
  uint32_t timer_ms = 0;
};

Host& host_of(const clap_host_t* h) { return *static_cast<Host*>(h->host_data); }

void h_log(const clap_host_t* h, clap_log_severity, const char* msg) {
  host_of(h).logs.emplace_back(msg);
}
bool h_register(const clap_host_t* h, uint32_t period_ms, clap_id* id) {
  host_of(h).timer_ms = period_ms;
  *id = host_of(h).timer = 7;
  return true;
}
bool h_unregister(const clap_host_t* h, clap_id id) {
  if (id == host_of(h).timer) host_of(h).timer = CLAP_INVALID_ID;
  return true;
}
const clap_host_log_t kLog{h_log};
const clap_host_timer_support_t kTimers{h_register, h_unregister};

const void* h_ext(const clap_host_t*, const char* id) {
  if (std::string(id) == CLAP_EXT_LOG) return &kLog;
  if (std::string(id) == CLAP_EXT_TIMER_SUPPORT) return &kTimers;
  return nullptr;
}
void h_noop(const clap_host_t*) {}

// ---- event list -------------------------------------------------------------

struct Events {
  std::vector<clap_event_note_t> notes;
  clap_input_events_t in{this, size, get};
  static uint32_t size(const clap_input_events_t* l) {
    return static_cast<uint32_t>(static_cast<const Events*>(l->ctx)->notes.size());
  }
  static const clap_event_header_t* get(const clap_input_events_t* l, uint32_t i) {
    return &static_cast<const Events*>(l->ctx)->notes[i].header;
  }
  void note(uint16_t type, uint32_t time, int16_t key, double vel = 0.8) {
    clap_event_note_t e{};
    e.header = {sizeof(e), time, CLAP_CORE_EVENT_SPACE_ID, type, 0};
    e.note_id = -1;
    e.channel = 0;
    e.key = key;
    e.velocity = vel;
    notes.push_back(e);
  }
};

bool o_push(const clap_output_events_t*, const clap_event_header_t*) { return true; }
const clap_output_events_t kOut{nullptr, o_push};

// ---- one plugin instance ----------------------------------------------------

struct Session {
  Host host;
  const clap_plugin_t* p = nullptr;
  std::vector<float> l, r, in_l, in_r;
  float* outs[2]{};
  float* ins[2]{};

  explicit Session(const char* id, uint32_t block = 256)
      : l(block), r(block), in_l(block), in_r(block) {
    host.clap = {CLAP_VERSION_INIT, &host, "test-host", "ratsnest", "", "1",
                 h_ext, h_noop, h_noop, h_noop};
    const auto* f = static_cast<const clap_plugin_factory_t*>(
        g_entry->get_factory(CLAP_PLUGIN_FACTORY_ID));
    p = f->create_plugin(f, &host.clap, id);
    REQUIRE(p != nullptr);
    REQUIRE(p->init(p));
    REQUIRE(p->activate(p, 48000.0, 1, block));
    REQUIRE(p->start_processing(p));
    outs[0] = l.data(); outs[1] = r.data();
    ins[0] = in_l.data(); ins[1] = in_r.data();
  }
  ~Session() {
    p->stop_processing(p);
    p->deactivate(p);
    p->destroy(p);
  }

  void run(Events& ev) {
    clap_audio_buffer_t out{outs, nullptr, 2, 0, 0};
    clap_audio_buffer_t in{ins, nullptr, 2, 0, 0};
    clap_process_t pr{};
    pr.frames_count = static_cast<uint32_t>(l.size());
    pr.audio_inputs = &in;
    pr.audio_inputs_count = 1;
    pr.audio_outputs = &out;
    pr.audio_outputs_count = 1;
    pr.in_events = &ev.in;
    pr.out_events = &kOut;
    REQUIRE(p->process(p, &pr) == CLAP_PROCESS_CONTINUE);
    ev.notes.clear();
  }
  void run() { Events ev; run(ev); }

  // Fires the host timer until the plugin's output changes from `before`.
  bool reload_until(float before) {
    const auto* t = static_cast<const clap_plugin_timer_support_t*>(
        p->get_extension(p, CLAP_EXT_TIMER_SUPPORT));
    for (int i = 0; i < 100; ++i) {
      t->on_timer(p, host.timer);
      for (int b = 0; b < 20; ++b) run();  // past the 50ms crossfade
      if (std::abs(l.back() - before) > 1e-4f) return true;
      std::this_thread::sleep_for(std::chrono::milliseconds(30));
    }
    return false;
  }
};

size_t first_nonzero(const std::vector<float>& v) {
  for (size_t i = 0; i < v.size(); ++i)
    if (v[i] != 0.0f) return i;
  return v.size();
}

const char* kKeys = "mid = midi {}\nmid.gate1 -> out.in : 0.5\n";
const char* kFx = "x = extin {}\nx -> out.in : 2\n";

const clap_plugin_descriptor_t* descriptor(uint32_t i) {
  const auto* f = static_cast<const clap_plugin_factory_t*>(
      g_entry->get_factory(CLAP_PLUGIN_FACTORY_ID));
  return f->get_plugin_descriptor(f, i);
}

bool has_feature(const clap_plugin_descriptor_t* d, const char* want) {
  for (const char* const* f = d->features; *f; ++f)
    if (std::string(*f) == want) return true;
  return false;
}

}  // namespace

TEST_CASE("each patch in the folder is a plugin, typed by what it reads") {
  const auto* f = static_cast<const clap_plugin_factory_t*>(
      g_entry->get_factory(CLAP_PLUGIN_FACTORY_ID));
  REQUIRE(f->get_plugin_count(f) == 3);
  CHECK(std::string(descriptor(0)->id) == "org.ratsnest.patch.broken");
  CHECK(std::string(descriptor(1)->name) == "fx");
  CHECK(has_feature(descriptor(1), CLAP_PLUGIN_FEATURE_AUDIO_EFFECT));
  CHECK(std::string(descriptor(2)->name) == "keys");
  CHECK(has_feature(descriptor(2), CLAP_PLUGIN_FEATURE_INSTRUMENT));
}

TEST_CASE("host notes land on their frame and release on note-off") {
  Session s("org.ratsnest.patch.keys");
  s.run();
  CHECK(first_nonzero(s.l) == s.l.size());  // nothing played: silence

  Events ev;
  ev.note(CLAP_EVENT_NOTE_ON, 100, 60);
  s.run(ev);
  CHECK(first_nonzero(s.l) == 100);
  CHECK(s.l.back() == doctest::Approx(0.5f));
  CHECK(s.r.back() == doctest::Approx(0.5f));

  ev.note(CLAP_EVENT_NOTE_OFF, 30, 60);
  s.run(ev);
  CHECK(s.l[29] == doctest::Approx(0.5f));
  CHECK(s.l[30] == 0.0f);
}

TEST_CASE("raw MIDI from the host plays too") {
  Session s("org.ratsnest.patch.keys");
  struct Midi { clap_event_midi_t e; } m{};
  m.e.header = {sizeof(m.e), 10, CLAP_CORE_EVENT_SPACE_ID, CLAP_EVENT_MIDI, 0};
  m.e.data[0] = 0x90; m.e.data[1] = 64; m.e.data[2] = 100;
  struct One {
    const clap_event_header_t* h;
    static uint32_t size(const clap_input_events_t*) { return 1; }
    static const clap_event_header_t* get(const clap_input_events_t* l, uint32_t) {
      return static_cast<const One*>(l->ctx)->h;
    }
  } one{&m.e.header};
  clap_input_events_t in{&one, One::size, One::get};
  clap_audio_buffer_t out{s.outs, nullptr, 2, 0, 0};
  clap_process_t pr{};
  pr.frames_count = static_cast<uint32_t>(s.l.size());
  pr.audio_outputs = &out;
  pr.audio_outputs_count = 1;
  pr.in_events = &in;
  pr.out_events = &kOut;
  REQUIRE(s.p->process(s.p, &pr) == CLAP_PROCESS_CONTINUE);
  CHECK(first_nonzero(s.l) == 10);
}

TEST_CASE("an effect patch hears the host's input") {
  Session s("org.ratsnest.patch.fx");
  for (size_t i = 0; i < s.in_l.size(); ++i) {
    s.in_l[i] = 0.25f * std::sin(0.05f * static_cast<float>(i));
    s.in_r[i] = s.in_l[i];
  }
  s.run();
  for (size_t i = 0; i < s.l.size(); i += 37) CHECK(s.l[i] == doctest::Approx(2 * s.in_l[i]));
}

TEST_CASE("saving the patch hot-reloads it; a bad save keeps the last good graph") {
  write_file(g_dir / "keys.patch", kKeys);
  Session s("org.ratsnest.patch.keys");
  Events ev;
  ev.note(CLAP_EVENT_NOTE_ON, 0, 60);
  s.run(ev);
  REQUIRE(s.l.back() == doctest::Approx(0.5f));
  CHECK(s.host.timer_ms > 0);

  std::this_thread::sleep_for(std::chrono::milliseconds(20));
  write_file(g_dir / "keys.patch", "mid = midi {}\nmid.gate1 -> out.in : 0.25\n");
  REQUIRE(s.reload_until(0.5f));
  CHECK(s.l.back() == doctest::Approx(0.25f));

  write_file(g_dir / "keys.patch", "mid = midi {}\nmid.gate1 -> nowhere.in\n");
  bool reported = false;
  for (int i = 0; i < 100 && !reported; ++i) {
    s.reload_until(0.25f);
    for (const auto& line : s.host.logs) reported |= line.find(":2:") != std::string::npos;
  }
  CHECK(reported);
  CHECK(s.l.back() == doctest::Approx(0.25f));  // still the last good graph
  write_file(g_dir / "keys.patch", kKeys);
}

TEST_CASE("a broken patch still instantiates, silent, with line:col in the log") {
  Session s("org.ratsnest.patch.broken");
  s.run();
  CHECK(first_nonzero(s.l) == s.l.size());
  bool located = false;
  for (const auto& line : s.host.logs) located |= line.find("broken.patch:1:") != std::string::npos;
  CHECK(located);
}

int main(int argc, char** argv) {
  if (argc < 3) {
    std::fprintf(stderr, "usage: clap_host_test <ratsnest.clap> <scratch dir>\n");
    return 2;
  }
  g_plugin_path = argv[1];
  g_dir = argv[2];
  fs::remove_all(g_dir);
  fs::create_directories(g_dir);
  write_file(g_dir / "keys.patch", kKeys);
  write_file(g_dir / "fx.patch", kFx);
  write_file(g_dir / "broken.patch", "osc = nonexistent {}\nosc -> out.in\n");
  write_file(g_dir / "notes.txt", "not a patch\n");
  setenv("RATSNEST_PLUGIN_DIR", g_dir.c_str(), 1);

  void* lib = dlopen(g_plugin_path.c_str(), RTLD_NOW | RTLD_LOCAL);
  if (!lib) {
    std::fprintf(stderr, "dlopen: %s\n", dlerror());
    return 1;
  }
  g_entry = static_cast<const clap_plugin_entry_t*>(dlsym(lib, "clap_entry"));
  if (!g_entry || !g_entry->init(g_plugin_path.c_str())) return 1;

  doctest::Context ctx;
  ctx.applyCommandLine(argc - 2, argv + 2);
  const int rc = ctx.run();
  g_entry->deinit();
  dlclose(lib);
  return rc;
}
