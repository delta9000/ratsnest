#include <algorithm>
#include <atomic>
#include <chrono>
#include <csignal>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <memory>
#include <sstream>
#include <string>
#include <thread>
#include <vector>
#include "dsl/parser.h"
#include "engine/controller.h"
#include "engine/engine.h"
#include "engine/pipewire_out.h"
#include "engine/rt_runner.h"
#include "graph/graph.h"
#include "io/wav.h"
#include "io/watcher.h"

namespace {

int usage() {
  std::fprintf(stderr,
      "usage:\n"
      "  modsynth check  <patch>\n"
      "  modsynth render <patch> [-d <seconds|Ns>] [-o <out.wav>] [--rate <hz>]\n"
      "  modsynth play   <patch> [--latency <N/RATE>] [--rate <hz>] [--watch <mod[.port]>]\n");
  return 2;
}

bool read_file(const std::string& path, std::string& out) {
  std::ifstream f(path);
  if (!f) return false;
  std::ostringstream ss;
  ss << f.rdbuf();
  out = ss.str();
  return true;
}

// Returns compiled graph or nullptr after printing diagnostics.
std::unique_ptr<ms::graph::Graph> load(const std::string& path, double rate) {
  std::string src;
  if (!read_file(path, src)) {
    std::fprintf(stderr, "error: cannot read '%s'\n", path.c_str());
    return nullptr;
  }
  auto p = ms::dsl::parse(src);
  auto report = [&](const ms::Diags& ds) {
    for (const auto& d : ds)
      std::fprintf(stderr, "%s:%d:%d: error: %s\n", path.c_str(), d.line, d.col,
                   d.msg.c_str());
  };
  if (!p.ok()) { report(p.errors); return nullptr; }
  auto c = ms::graph::compile(p.ast, rate);
  if (!c.ok()) { report(c.errors); return nullptr; }
  return std::move(c.graph);
}

double parse_duration(const char* s) {
  std::string v(s);
  if (!v.empty() && v.back() == 's') v.pop_back();
  return std::atof(v.c_str());
}

std::atomic<bool> g_stop{false};
void on_sigint(int) { g_stop.store(true); }

void render_trampoline(void* user, float* l, float* r, size_t frames) {
  static_cast<ms::engine::RtRunner*>(user)->render(l, r, frames);
}

void print_status(ms::engine::RtRunner& run,
                  const ms::engine::PipewireOut& pw,
                  bool watching) {
  auto bar = [](float v) {
    std::string b(20, '-');
    int fill = static_cast<int>(std::clamp(v, 0.0f, 1.0f) * 20.0f);
    for (int i = 0; i < fill; ++i) b[static_cast<size_t>(i)] = '#';
    return b;
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

int cmd_play(const std::string& patch, const char* latency, double rate,
             const char* watch) {
  std::string src;
  if (!read_file(patch, src)) {
    std::fprintf(stderr, "error: cannot read '%s'\n", patch.c_str());
    return 1;
  }

  ms::engine::RtRunner runner(rate);
  ms::engine::Controller ctl(runner, rate);

  auto report = [&](const ms::Diags& ds) {
    std::fputs("\n", stderr);
    for (const auto& d : ds)
      std::fprintf(stderr, "%s:%d:%d: error: %s\n", patch.c_str(), d.line,
                   d.col, d.msg.c_str());
  };

  auto first = ctl.load(src);
  if (!first.ok) { report(first.errors); return 1; }
  if (watch != nullptr && !ctl.set_watch(watch)) {
    std::fprintf(stderr, "error: cannot watch '%s' (unknown module or port)\n", watch);
    return 1;
  }

  auto pw = std::make_unique<ms::engine::PipewireOut>(
      "modsynth", &render_trampoline, &runner, latency,
      static_cast<uint32_t>(rate));
  ms::io::PatchWatcher watcher(patch);
  if (!watcher.ok())
    std::fprintf(stderr, "warning: cannot watch '%s'; hot-reload disabled\n",
                 patch.c_str());

  std::signal(SIGINT, &on_sigint);
  std::fprintf(stderr, "playing %s  (edit + save to hot-reload, Ctrl-C to quit)\n",
               patch.c_str());

  bool warned_rate = false;
  // Deviation (owner-approved, see task-9 context): PW_STREAM_STATE_UNCONNECTED
  // does not surface through pw->error() -- it's a silent disconnect. Track
  // "was ok, now isn't" so a >2s silent outage also triggers the reconnect
  // path below, not just an explicit error() string.
  bool prev_ok = false;
  bool down_since_set = false;
  std::chrono::steady_clock::time_point down_since{};
  while (!g_stop.load()) {
    if (watcher.ok()) {
      if (watcher.poll_changed(100)) {
        if (!read_file(patch, src)) {
          std::fprintf(stderr, "\nerror: cannot re-read '%s'\n", patch.c_str());
        } else {
          auto res = ctl.load(src);
          if (!res.ok) report(res.errors);
          else std::fprintf(stderr, "\nreloaded (%s)\n",
                            res.topology ? "topology" : "params");
        }
      }
    } else {
      // Hot-reload is disabled: poll_changed() would return immediately
      // instead of blocking, so without this the loop spins at full CPU.
      // Sleep to keep the same ~10 Hz cadence as the watcher-enabled path.
      std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }
    ctl.drain_retired();
    print_status(runner, *pw, watch != nullptr);

    const bool cur_ok = pw->ok();
    if (cur_ok) {
      prev_ok = true;
      down_since_set = false;
    } else if (prev_ok && !down_since_set) {
      down_since = std::chrono::steady_clock::now();
      down_since_set = true;
    }
    const bool silent_disconnect =
        down_since_set &&
        std::chrono::steady_clock::now() - down_since > std::chrono::seconds(2);

    // Spec 7.5: PipeWire disconnects never kill the session. Recreate the
    // backend with a 2s backoff; the graph keeps its state and resumes.
    const std::string pw_err = pw->error();
    if (!pw_err.empty() || silent_disconnect) {
      std::string reason = !pw_err.empty() ? pw_err : "disconnected";
      std::fprintf(stderr, "\nwarning: PipeWire: %s -- reconnecting...\n",
                   reason.c_str());
      pw.reset();
      std::this_thread::sleep_for(std::chrono::seconds(2));
      pw = std::make_unique<ms::engine::PipewireOut>(
          "modsynth", &render_trampoline, &runner, latency,
          static_cast<uint32_t>(rate));
      prev_ok = false;
      down_since_set = false;
    }
    if (!warned_rate && pw->observed_rate() != 0 &&
        pw->observed_rate() != static_cast<uint32_t>(rate)) {
      std::fprintf(stderr,
          "\nwarning: PipeWire runs at %u Hz but the patch was compiled for"
          " %g Hz; restart with --rate %u\n",
          pw->observed_rate(), rate, pw->observed_rate());
      warned_rate = true;
    }
  }
  std::fputs("\n", stderr);
  return 0;
}

}  // namespace

int main(int argc, char** argv) {
  if (argc < 3) return usage();
  const std::string cmd = argv[1];
  const std::string patch = argv[2];

  if (cmd == "check") {
    if (!load(patch, 48000.0)) return 1;
    std::printf("ok\n");
    return 0;
  }

  if (cmd == "render") {
    double seconds = 10.0, rate = 48000.0;
    std::string out_path = "out.wav";
    for (int i = 3; i < argc; ++i) {
      if (!std::strcmp(argv[i], "-d") && i + 1 < argc) seconds = parse_duration(argv[++i]);
      else if (!std::strcmp(argv[i], "-o") && i + 1 < argc) out_path = argv[++i];
      else if (!std::strcmp(argv[i], "--rate") && i + 1 < argc) rate = std::atof(argv[++i]);
      else return usage();
    }
    if (seconds <= 0.0) { std::fprintf(stderr, "error: duration must be positive\n"); return 2; }

    auto g = load(patch, rate);
    if (!g) return 1;
    ms::engine::Engine engine(std::move(g));

    const size_t frames = static_cast<size_t>(seconds * rate);
    std::vector<float> l(frames), r(frames), interleaved(frames * 2);
    engine.process(l.data(), r.data(), frames);
    for (size_t i = 0; i < frames; ++i) {
      interleaved[2 * i] = l[i];
      interleaved[2 * i + 1] = r[i];
    }
    if (!ms::io::write_wav_f32(out_path, interleaved.data(), frames, 2,
                               static_cast<int>(rate))) {
      std::fprintf(stderr, "error: cannot write '%s'\n", out_path.c_str());
      return 1;
    }
    std::printf("rendered %.1fs -> %s\n", seconds, out_path.c_str());
    return 0;
  }

  if (cmd == "play") {
    const char* latency = nullptr;
    const char* watch = nullptr;
    double rate = 48000.0;
    for (int i = 3; i < argc; ++i) {
      if (!std::strcmp(argv[i], "--latency") && i + 1 < argc) latency = argv[++i];
      else if (!std::strcmp(argv[i], "--rate") && i + 1 < argc) rate = std::atof(argv[++i]);
      else if (!std::strcmp(argv[i], "--watch") && i + 1 < argc) watch = argv[++i];
      else return usage();
    }
    return cmd_play(patch, latency, rate, watch);
  }

  return usage();
}
