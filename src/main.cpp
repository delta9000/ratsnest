#include <algorithm>
#include <atomic>
#include <chrono>
#include <cmath>
#include <csignal>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <memory>
#include <sstream>
#include <string>
#include <thread>
#include <vector>
#include "dsl/parser.h"
#include "engine/controller.h"
#include "engine/engine.h"
#include "engine/audio_in.h"
#include "engine/midi_in.h"
#include "engine/pipewire_out.h"
#include "engine/rt_runner.h"
#include "graph/graph.h"
#include "graph/subs.h"
#include "io/wav.h"
#include "io/watcher.h"
#include "rt/denormal.h"

namespace {

int usage() {
  std::fprintf(stderr,
      "usage:\n"
      "  ratsnest describe [module_type]   # JSON module interfaces from the registry\n"
      "  ratsnest check  <patch>\n"
      "  ratsnest render <patch> [-d <seconds|Ns>] [--start <seconds|Ns>] [-o <out.wav>]\n"
      "                          [--rate <hz>] [--stats [Ns]]\n"
      "  ratsnest play   <patch> [--latency <N/RATE>] [--rate <hz>] [--watch <mod[.port]>]\n");
  return 2;
}

// ---- describe ---------------------------------------------------------------

// Registry names and enum values are built-in identifiers, never user strings.
// Keep discovery beside validation's metadata so agents see the actual surface.
const char* config_kind(const ms::dsp::ConfigInfo& c) {
  using K = ms::dsp::ConfigKind;
  if (c.kind == K::Enums) return "enum_array";
  if (c.enum_vals) return "enum";
  if (c.kind == K::Numbers) return "number_array";
  return c.kind == K::Text ? "text" : "number";
}

void describe_config(const ms::dsp::ConfigInfo& c) {
  std::printf("{\"name\":\"%s\",\"kind\":\"%s\"", c.name, config_kind(c));
  if (c.enum_vals) {
    std::printf(",\"values\":[");
    for (size_t j = 0; c.enum_vals[j]; ++j)
      std::printf("%s\"%s\"", j ? "," : "", c.enum_vals[j]);
    std::printf("]");
  }
  std::printf("}");
}

void describe(const ms::dsp::ModuleInfo& m) {
  std::printf("{\"type\":\"%s\",\"inputs\":[", m.type);
  for (size_t i = 0; i < m.inputs.size(); ++i) {
    const auto& in = m.inputs[i];
    std::printf("%s{\"name\":\"%s\",\"base\":%.9g,\"glide\":%s}", i ? "," : "", in.name,
                static_cast<double>(in.def), in.glide ? "true" : "false");
  }
  std::printf("],\"configs\":[");
  for (size_t i = 0; i < m.configs.size(); ++i) {
    std::printf("%s", i ? "," : "");
    describe_config(m.configs[i]);
  }
  std::printf("],\"outputs\":[");
  for (size_t i = 0; i < m.outputs.size(); ++i)
    std::printf("%s\"%s\"", i ? "," : "", m.outputs[i].name);
  std::printf("],\"default_output\":");
  if (m.outputs.empty()) std::printf("null");
  else std::printf("\"%s\"", m.outputs[0].name);
  std::printf("}");
}

int cmd_describe(int argc, char** argv) {
  if (argc > 3) return usage();
  if (argc == 3) {
    const auto* m = ms::dsp::find_module(argv[2]);
    if (!m) {
      std::fprintf(stderr, "error: unknown module type '%s'\n", argv[2]);
      return 1;
    }
    describe(*m);
  } else {
    std::printf("[");
    const char* sep = "";
    for (const auto* m : ms::dsp::all_modules()) {
      std::printf("%s", sep);
      describe(*m);
      sep = ",\n";
    }
    std::printf("]");
  }
  std::printf("\n");
  return 0;
}

// ---- loading ----------------------------------------------------------------

bool read_file(const std::string& path, std::string& out) {
  std::ifstream f(path);
  if (!f) return false;
  std::ostringstream ss;
  ss << f.rdbuf();
  out = ss.str();
  return true;
}

// Diagnostics without a file of their own belong to the patch itself.
void report(const std::string& patch, const ms::Diags& ds) {
  for (const auto& d : ds)
    std::fprintf(stderr, "%s:%d:%d: error: %s\n",
                 d.file.empty() ? patch.c_str() : d.file.c_str(),
                 d.line, d.col, d.msg.c_str());
}

// Returns compiled graph or nullptr after printing diagnostics.
std::unique_ptr<ms::graph::Graph> load(const std::string& path, double rate) {
  std::string src;
  if (!read_file(path, src)) {
    std::fprintf(stderr, "error: cannot read '%s'\n", path.c_str());
    return nullptr;
  }
  auto p = ms::dsl::parse(src);
  if (!p.ok()) { report(path, p.errors); return nullptr; }
  std::filesystem::path pp(path);
  const std::string dir = pp.has_parent_path() ? pp.parent_path().string() : ".";
  ms::graph::expand_subs(p.ast, dir, ms::graph::read_text_file, p.errors);
  if (!p.errors.empty()) { report(path, p.errors); return nullptr; }
  auto c = ms::graph::compile(p.ast, rate);
  if (!c.ok()) { report(path, c.errors); return nullptr; }
  return std::move(c.graph);
}

int cmd_check(const std::string& patch) {
  if (!load(patch, 48000.0)) return 1;
  std::printf("ok\n");
  return 0;
}

// ---- argument parsing -------------------------------------------------------

double parse_duration(const char* s) {
  std::string v(s);
  if (!v.empty() && v.back() == 's') v.pop_back();
  return std::atof(v.c_str());
}

// When argv[i] is `name` and a value follows, consumes both and returns the
// value; otherwise returns nullptr and leaves i alone.
const char* option(int argc, char** argv, int& i, const char* name) {
  if (std::strcmp(argv[i], name) != 0 || i + 1 >= argc) return nullptr;
  return argv[++i];
}

// 20-cell text meter with `fill` cells lit.
std::string meter(int fill) {
  std::string b(20, '-');
  for (int i = 0; i < fill; ++i) b[static_cast<size_t>(i)] = '#';
  return b;
}

// ---- render -----------------------------------------------------------------

struct RenderOpts {
  double seconds = 10.0, rate = 48000.0, start_sec = 0.0;
  std::string out_path = "out.wav";
  double stats_window = 0.0;  // 0 = no stats
};

bool parse_render(int argc, char** argv, RenderOpts& o) {
  for (int i = 3; i < argc; ++i) {
    if (const char* v = option(argc, argv, i, "-d")) o.seconds = parse_duration(v);
    else if (const char* v = option(argc, argv, i, "-o")) o.out_path = v;
    else if (const char* v = option(argc, argv, i, "--start")) o.start_sec = parse_duration(v);
    else if (const char* v = option(argc, argv, i, "--rate")) o.rate = std::atof(v);
    else if (!std::strcmp(argv[i], "--stats")) {
      // The window length is optional: take the next word only if it is one.
      o.stats_window = 10.0;
      if (i + 1 < argc && parse_duration(argv[i + 1]) > 0.0)
        o.stats_window = parse_duration(argv[++i]);
    }
    else return false;
  }
  return true;
}

// Peak, energy and DC over frames [begin, end) of both channels.
struct Level {
  double peak = 0.0, sumsq = 0.0, dc = 0.0;
};
Level measure(const std::vector<float>& l, const std::vector<float>& r,
              size_t begin, size_t end) {
  Level m;
  for (size_t i = begin; i < end; ++i) {
    const double a = l[i], b = r[i];
    m.peak = std::max({m.peak, std::abs(a), std::abs(b)});
    m.sumsq += a * a + b * b;
    m.dc += a + b;
  }
  return m;
}

double db(double v) { return 20.0 * std::log10(std::max(v, 1e-12)); }

void print_stats(const std::vector<float>& l, const std::vector<float>& r,
                 const RenderOpts& o) {
  const size_t frames = l.size();
  const double n = 2.0 * static_cast<double>(frames);
  const Level all = measure(l, r, 0, frames);
  std::printf("peak %6.1f dBFS   rms %6.1f dBFS   dc %+.5f\n",
              db(all.peak), db(std::sqrt(all.sumsq / n)), all.dc / n);
  const size_t win = static_cast<size_t>(o.stats_window * o.rate);
  for (size_t start = 0; start < frames; start += win) {
    const size_t end = std::min(frames, start + win);
    const Level w = measure(l, r, start, end);
    const double wrms = db(std::sqrt(w.sumsq / (2.0 * static_cast<double>(end - start))));
    const int fill = static_cast<int>(std::clamp((wrms + 60.0) / 60.0, 0.0, 1.0) * 20.0);
    std::printf("%7.1fs  rms %6.1f  peak %6.1f  |%s|\n",
                o.start_sec + static_cast<double>(start) / o.rate, wrms, db(w.peak),
                meter(fill).c_str());
  }
}

// A stateful graph has no seek: --start runs it from zero and discards the
// skipped samples. Block size cannot change the result (Engine::process is
// per-sample; tests/test_integration.cpp pins it), so chunk the discard
// instead of allocating a buffer for it.
void skip_frames(ms::engine::Engine& engine, size_t skip) {
  constexpr size_t kChunk = 4096;
  std::vector<float> sl(kChunk), sr(kChunk);
  for (size_t done = 0; done < skip;) {
    const size_t n = std::min(kChunk, skip - done);
    engine.process(sl.data(), sr.data(), n);
    done += n;
  }
}

bool write_stereo(const RenderOpts& o, const std::vector<float>& l,
                  const std::vector<float>& r) {
  std::vector<float> interleaved(l.size() * 2);
  for (size_t i = 0; i < l.size(); ++i) {
    interleaved[2 * i] = l[i];
    interleaved[2 * i + 1] = r[i];
  }
  return ms::io::write_wav_f32(o.out_path, interleaved.data(), l.size(), 2,
                               static_cast<int>(o.rate));
}

int cmd_render(const std::string& patch, int argc, char** argv) {
  RenderOpts o;
  if (!parse_render(argc, argv, o)) return usage();
  if (o.seconds <= 0.0) { std::fprintf(stderr, "error: duration must be positive\n"); return 2; }
  if (o.start_sec < 0.0) { std::fprintf(stderr, "error: --start must not be negative\n"); return 2; }

  auto g = load(patch, o.rate);
  if (!g) return 1;
  ms::engine::Engine engine(std::move(g));

  const size_t frames = static_cast<size_t>(o.seconds * o.rate);
  const size_t skip = static_cast<size_t>(o.start_sec * o.rate);
  std::vector<float> l(frames), r(frames);
  // Decaying filter tails hit denormals; match the PipeWire thread's FTZ.
  ms::rt::flush_denormals_on_this_thread();
  skip_frames(engine, skip);
  engine.process(l.data(), r.data(), frames);
  if (!write_stereo(o, l, r)) {
    std::fprintf(stderr, "error: cannot write '%s'\n", o.out_path.c_str());
    return 1;
  }
  if (skip > 0)
    std::printf("rendered %.1fs from %.1fs -> %s\n", o.seconds, o.start_sec, o.out_path.c_str());
  else
    std::printf("rendered %.1fs -> %s\n", o.seconds, o.out_path.c_str());
  if (o.stats_window > 0.0) print_stats(l, r, o);
  return 0;
}

// ---- play -------------------------------------------------------------------

std::atomic<bool> g_stop{false};
void on_sigint(int) { g_stop.store(true); }

void render_trampoline(void* user, float* l, float* r, size_t frames) {
  static_cast<ms::engine::RtRunner*>(user)->render(l, r, frames);
}

void print_status(ms::engine::RtRunner& run,
                  const ms::engine::PipewireOut& pw,
                  bool watching) {
  auto bar = [](float v) {
    return meter(static_cast<int>(std::clamp(v, 0.0f, 1.0f) * 20.0f));
  };
  std::string line = "\r  L |" + bar(run.peak_l()) + "|  R |" + bar(run.peak_r()) +
                     "|  xruns " + std::to_string(pw.xruns());
  if (watching) {
    float v = 0.0f, last = 0.0f;
    bool got = false;
    while (run.try_watch_value(v)) { last = v; got = true; }
    if (got) {
      char buf[32];
      std::snprintf(buf, sizeof(buf), "  watch=% .3f", static_cast<double>(last));
      line += buf;
    }
  }
  std::fputs(line.c_str(), stderr);
  std::fflush(stderr);
}

// Sleeps up to `ms` in short slices so Ctrl-C is honored promptly. Returns
// false when interrupted.
bool sleep_unless_stopped(int ms) {
  for (int waited_ms = 0; waited_ms < ms && !g_stop.load(); waited_ms += 100)
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
  return !g_stop.load();
}

// Hot-reload: watches the patch and every sub file it pulled in, reloading
// through the controller on save. The watched set follows the latest good
// load's file list.
class HotReload {
 public:
  HotReload(std::string patch, const std::vector<std::string>& files)
      : patch_(std::move(patch)) {
    watch(files);
    if (!watching_)
      std::fprintf(stderr, "warning: cannot watch '%s'; hot-reload disabled\n",
                   patch_.c_str());
  }

  // Waits ~100 ms for a save, then reloads if one arrived.
  void poll(ms::engine::Controller& ctl) {
    if (!watching_) {
      // poll_changed() would return immediately instead of blocking, so
      // without this the loop spins at full CPU. Sleep to keep the same
      // ~10 Hz cadence as the watcher-enabled path.
      std::this_thread::sleep_for(std::chrono::milliseconds(100));
      return;
    }
    if (watcher_->poll_changed(100)) reload(ctl);
  }

 private:
  void watch(const std::vector<std::string>& files) {
    std::vector<std::string> want{patch_};
    want.insert(want.end(), files.begin(), files.end());
    if (watcher_ && want == watched_) return;
    watched_ = std::move(want);
    watcher_ = std::make_unique<ms::io::PatchWatcher>(watched_);
    watching_ = watcher_->ok();
  }

  void reload(ms::engine::Controller& ctl) {
    std::string src;
    if (!read_file(patch_, src)) {
      std::fprintf(stderr, "\nerror: cannot re-read '%s'\n", patch_.c_str());
      return;
    }
    auto res = ctl.load(src, patch_);
    if (!res.ok) {
      std::fputs("\n", stderr);
      report(patch_, res.errors);
      return;
    }
    std::fprintf(stderr, "\nreloaded (%s)\n", res.topology ? "topology" : "params");
    watch(res.files);
  }

  std::string patch_;
  std::vector<std::string> watched_;
  std::unique_ptr<ms::io::PatchWatcher> watcher_;
  bool watching_ = false;
};

// Spec 7.5: PipeWire disconnects never kill the session. The output stream is
// recreated with a 2s backoff; the graph keeps its state and resumes.
class AudioOut {
 public:
  AudioOut(ms::engine::RtRunner& runner, const char* latency, double rate)
      : runner_(runner), latency_(latency), rate_(rate) {
    open();
  }
  const ms::engine::PipewireOut& pw() const { return *pw_; }

  // Returns false only when Ctrl-C interrupted a reconnect.
  bool supervise() {
    const std::string reason = failure();
    if (reason.empty()) return true;
    std::fprintf(stderr, "\nwarning: PipeWire: %s -- reconnecting...\n", reason.c_str());
    pw_.reset();
    if (!sleep_unless_stopped(2000)) return false;
    open();
    return true;
  }

  void warn_rate_mismatch() {
    const uint32_t seen = pw_->observed_rate();
    if (warned_rate_ || seen == 0 || seen == static_cast<uint32_t>(rate_)) return;
    std::fprintf(stderr,
        "\nwarning: PipeWire runs at %u Hz but the patch was compiled for"
        " %g Hz; restart with --rate %u\n",
        seen, rate_, seen);
    warned_rate_ = true;
  }

 private:
  void open() {
    pw_ = std::make_unique<ms::engine::PipewireOut>(
        "ratsnest", &render_trampoline, &runner_, latency_,
        static_cast<uint32_t>(rate_));
    prev_ok_ = false;
    down_since_set_ = false;
  }

  // Deviation (owner-approved, see task-9 context): PW_STREAM_STATE_UNCONNECTED
  // does not surface through pw->error() -- it's a silent disconnect. Track
  // "was ok, now isn't" so a >2s silent outage also triggers the reconnect,
  // not just an explicit error() string. Returns "" while healthy.
  std::string failure() {
    if (pw_->ok()) {
      prev_ok_ = true;
      down_since_set_ = false;
    } else if (prev_ok_ && !down_since_set_) {
      down_since_ = std::chrono::steady_clock::now();
      down_since_set_ = true;
    }
    const std::string err = pw_->error();
    if (!err.empty()) return err;
    const bool silent_disconnect =
        down_since_set_ &&
        std::chrono::steady_clock::now() - down_since_ > std::chrono::seconds(2);
    return silent_disconnect ? "disconnected" : "";
  }

  ms::engine::RtRunner& runner_;
  const char* latency_;
  double rate_;
  std::unique_ptr<ms::engine::PipewireOut> pw_;
  bool prev_ok_ = false;
  bool down_since_set_ = false;
  std::chrono::steady_clock::time_point down_since_{};
  bool warned_rate_ = false;
};

void warn_unavailable(const std::string& err, const char* what, const char* effect) {
  if (!err.empty())
    std::fprintf(stderr, "warning: %s unavailable (%s); %s\n", what, err.c_str(), effect);
}

struct PlayOpts {
  const char* latency = nullptr;
  const char* watch = nullptr;
  double rate = 48000.0;
};

bool parse_play(int argc, char** argv, PlayOpts& o) {
  for (int i = 3; i < argc; ++i) {
    if (const char* v = option(argc, argv, i, "--latency")) o.latency = v;
    else if (const char* v = option(argc, argv, i, "--rate")) o.rate = std::atof(v);
    else if (const char* v = option(argc, argv, i, "--watch")) o.watch = v;
    else return false;
  }
  return true;
}

int cmd_play(const std::string& patch, int argc, char** argv) {
  PlayOpts o;
  if (!parse_play(argc, argv, o)) return usage();
  std::string src;
  if (!read_file(patch, src)) {
    std::fprintf(stderr, "error: cannot read '%s'\n", patch.c_str());
    return 1;
  }

  ms::engine::RtRunner runner(o.rate);
  ms::engine::Controller ctl(runner, o.rate);

  auto first = ctl.load(src, patch);
  if (!first.ok) {
    std::fputs("\n", stderr);
    report(patch, first.errors);
    return 1;
  }
  if (o.watch != nullptr && !ctl.set_watch(o.watch)) {
    std::fprintf(stderr, "error: cannot watch '%s' (unknown module or port)\n", o.watch);
    return 1;
  }

  AudioOut out(runner, o.latency, o.rate);

  // Constructed once for the whole play session and never touched again:
  // the port (and any patchbay connection to a controller) must survive
  // every hot-reload below (midi spec §4). A refused port is a guarded,
  // non-fatal condition -- keep playing audio-only (design rule 6 spirit).
  // error() is only ever set by a definite failure (port/filter refused at
  // connect time, or a later ERROR state); unlike ok() it does not race the
  // async PipeWire connection handshake, so it is safe to check once, right
  // here, without waiting.
  ms::engine::MidiIn midi_in("ratsnest-midi", runner);
  warn_unavailable(midi_in.error(), "MIDI input", "continuing audio-only");

  // Audio capture for `extin`, same lifecycle and non-fatal contract as MidiIn.
  ms::engine::AudioIn audio_in("ratsnest-capture", runner,
                               static_cast<uint32_t>(o.rate));
  warn_unavailable(audio_in.error(), "audio capture", "extin will be silent");

  HotReload reload(patch, first.files);
  std::signal(SIGINT, &on_sigint);
  std::fprintf(stderr, "playing %s  (edit + save to hot-reload, Ctrl-C to quit)\n",
               patch.c_str());

  while (!g_stop.load()) {
    reload.poll(ctl);
    ctl.drain_retired();
    print_status(runner, out.pw(), o.watch != nullptr);
    if (!out.supervise()) break;
    out.warn_rate_mismatch();
  }
  std::fputs("\n", stderr);
  return 0;
}

}  // namespace

int main(int argc, char** argv) {
  if (argc < 2) return usage();
  const std::string cmd = argv[1];
  if (cmd == "describe") return cmd_describe(argc, argv);
  if (argc < 3) return usage();
  const std::string patch = argv[2];
  if (cmd == "check") return cmd_check(patch);
  if (cmd == "render") return cmd_render(patch, argc, argv);
  if (cmd == "play") return cmd_play(patch, argc, argv);
  return usage();
}
