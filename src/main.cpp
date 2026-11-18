#include <algorithm>
#include <array>
#include <sys/stat.h>
#include <atomic>
#include <chrono>
#include <cmath>
#include <csignal>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <deque>
#include <limits>
#include <unistd.h>
#include <memory>
#include <string>
#include <thread>
#include <vector>
#include "dsl/suggest.h"
#include "engine/controller.h"
#include "engine/engine.h"
#include "engine/audio_in.h"
#include "engine/midi_in.h"
#include "engine/pipewire_out.h"
#include "engine/rt_runner.h"
#include "graph/graph.h"
#include "graph/subs.h"
#include "graph/tap.h"
#include "io/wav.h"
#include "io/watcher.h"
#include "rt/denormal.h"

namespace {

int usage() {
  std::fprintf(stderr,
      "usage:\n"
      "  ratsnest check  <patch>             validate; errors print as file:line:col\n"
      "  ratsnest render <patch> [options]   offline render to a stereo float WAV\n"
      "      -d <dur>          length (default 10s)\n"
      "      --start <dur>     begin the take this far in (the skipped part still runs)\n"
      "      -o <file.wav>     output path (default out.wav)\n"
      "      --rate <hz>       sample rate (default 48000)\n"
      "      --stats [<dur>]   print peak/RMS/DC and a level timeline per window (default 10s)\n"
      "      --tap NAME[=node[.port]]  also write <out>.NAME.wav from that endpoint (stereo\n"
      "                        if it has l/r or out_l/out_r) and print its stats; repeatable\n"
      "  ratsnest play   <patch> [options]   live PipeWire output; hot-reloads on save\n"
      "      --latency <N/RATE>  e.g. 256/48000\n"
      "      --rate <hz>         sample rate (default 48000)\n"
      "      --watch <mod[.port]>  show that signal's value in the status line\n"
      "  ratsnest describe [module_type]     module interfaces as JSON\n"
      "durations: 90, 90s, 500ms, 2m.  Modules and patch syntax: docs/modules.md\n");
  return 2;
}

// ---- describe ---------------------------------------------------------------

// Registry names and enum values are built-in identifiers, never user strings.
// Keep discovery beside validation's metadata so agents see the actual surface.
// The comma before every JSON array element but the first.
const char* sep(size_t i) { return i ? "," : ""; }

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
      std::printf("%s\"%s\"", sep(j), c.enum_vals[j]);
    std::printf("]");
  }
  std::printf("}");
}

void describe(const ms::dsp::ModuleInfo& m) {
  std::printf("{\"type\":\"%s\",\"inputs\":[", m.type);
  for (size_t i = 0; i < m.inputs.size(); ++i) {
    const auto& in = m.inputs[i];
    std::printf("%s{\"name\":\"%s\",\"base\":%.9g,\"glide\":%s}", sep(i), in.name,
                static_cast<double>(in.def), in.glide ? "true" : "false");
  }
  std::printf("],\"configs\":[");
  for (size_t i = 0; i < m.configs.size(); ++i) {
    std::printf("%s", sep(i));
    describe_config(m.configs[i]);
  }
  std::printf("],\"outputs\":[");
  for (size_t i = 0; i < m.outputs.size(); ++i)
    std::printf("%s\"%s\"", sep(i), m.outputs[i].name);
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
      std::fprintf(stderr, "error: unknown module type '%s'%s\n", argv[2],
                   ms::dsl::did_you_mean(argv[2], ms::graph::module_types()).c_str());
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

// Diagnostics without a file of their own belong to the patch itself.
void report(const std::string& patch, const ms::Diags& ds) {
  for (const auto& d : ds)
    std::fprintf(stderr, "%s:%d:%d: error: %s\n",
                 d.file.empty() ? patch.c_str() : d.file.c_str(),
                 d.line, d.col, d.msg.c_str());
}

// Reads and builds the patch; false after printing why it failed.
bool build(const std::string& path, double rate, std::string& src, ms::graph::PatchBuild& b) {
  if (!ms::graph::read_text_file(path, src)) {
    std::fprintf(stderr, "error: cannot read '%s'\n", path.c_str());
    return false;
  }
  b = ms::graph::build_patch(src, path, rate);
  if (!b.compiled.ok()) report(path, b.compiled.errors);
  return b.compiled.ok();
}

// Takes the built graph, warning if nothing reaches `out`.
std::unique_ptr<ms::graph::Graph> take_graph(const std::string& path, ms::graph::PatchBuild& b) {
  const auto& g = *b.compiled.graph;
  if (g.in_cables[static_cast<size_t>(g.out_node)].empty())
    std::fprintf(stderr, "%s: warning: nothing is cabled into 'out'; the patch is silent\n",
                 path.c_str());
  return std::move(b.compiled.graph);
}

// Returns compiled graph or nullptr after printing diagnostics.
std::unique_ptr<ms::graph::Graph> load(const std::string& path, double rate) {
  std::string src;
  ms::graph::PatchBuild b;
  return build(path, rate, src, b) ? take_graph(path, b) : nullptr;
}

int cmd_check(const std::string& patch) {
  if (!load(patch, 48000.0)) return 1;
  std::printf("ok\n");
  return 0;
}

// ---- argument parsing -------------------------------------------------------

// Seconds from "90", "90s", "500ms" or "2m"; NaN for anything else, so a
// typo is an error rather than a silently different length.
double parse_duration(const char* s) {
  char* end = nullptr;
  const double v = std::strtod(s, &end);
  const std::string unit = end;
  for (const auto& [u, k] : {std::pair{"", 1.0}, {"s", 1.0}, {"ms", 0.001}, {"m", 60.0}})
    if (end != s && unit == u) return v * k;
  return std::nan("");
}

// A flag the command does not take, or one missing its value.
bool bad_option(const char* arg) {
  std::fprintf(stderr, "error: unknown option '%s', or it is missing its value\n", arg);
  return false;
}

// --rate: a whole number of Hz in 8000..384000. Anything else is an error
// rather than a patch compiled for a rate no device or WAV can carry.
bool rate_option(const char* s, double& rate) {
  char* end = nullptr;
  const long hz = std::strtol(s, &end, 10);  // "" parses as 0: out of range
  rate = static_cast<double>(hz);
  if (*end == '\0' && hz == std::clamp(hz, 8000L, 384000L)) return true;
  std::fprintf(stderr, "error: --rate needs a whole sample rate from 8000 to 384000 Hz, not '%s'\n", s);
  return false;
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
  double stats_window = 10.0;  // also the window of each --tap's table
  bool stats = false;
  std::vector<ms::graph::Tap> taps;
};

bool parse_render(int argc, char** argv, RenderOpts& o) {
  for (int i = 3; i < argc; ++i) {
    if (const char* v = option(argc, argv, i, "-d")) o.seconds = parse_duration(v);
    else if (const char* v = option(argc, argv, i, "-o")) o.out_path = v;
    else if (const char* v = option(argc, argv, i, "--start")) o.start_sec = parse_duration(v);
    else if (const char* v = option(argc, argv, i, "--rate")) { if (!rate_option(v, o.rate)) return false; }
    else if (const char* v = option(argc, argv, i, "--tap")) o.taps.push_back(ms::graph::parse_tap(v));
    else if (!std::strcmp(argv[i], "--stats")) {
      // The window length is optional: take the next word only if it is one.
      o.stats = true;
      if (i + 1 < argc && parse_duration(argv[i + 1]) > 0.0)
        o.stats_window = parse_duration(argv[++i]);
    }
    else return bad_option(argv[i]);
  }
  return true;
}

struct Level {
  double peak = 0.0, sumsq = 0.0, dc = 0.0;
  void add(float left, float right) {
    const double a = left, b = right;
    peak = std::max({peak, std::abs(a), std::abs(b)});
    sumsq += a * a + b * b;
    dc += a + b;
  }
};

double db(double v) { return 20.0 * std::log10(std::max(v, 1e-12)); }

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

constexpr size_t kRenderBlock = 4096;

// Stage beside the destination so a failed render leaves an existing mix
// intact. Each mix/stem holds one block of samples and one level per window.
class RenderFile {
 public:
  RenderFile(std::string path, size_t channels, bool stats)
      : path_(std::move(path)), channels_(channels), stats_(stats),
        samples_(kRenderBlock * channels) {}
  RenderFile(const RenderFile&) = delete;
  ~RenderFile() {
    if (file_) std::fclose(file_);
    if (!temp_.empty()) std::remove(temp_.c_str());
  }

  bool open(size_t frames, int rate) {
    struct stat st{};
    const bool exists = lstat(path_.c_str(), &st) == 0;
    if (exists && (!S_ISREG(st.st_mode) || st.st_nlink > 1)) {
      // Preserve links and device outputs such as /dev/null.
      file_ = std::fopen(path_.c_str(), "wb");
    } else {
      if (exists && access(path_.c_str(), W_OK) != 0) return failed();
      std::string name = path_ + ".XXXXXX";
      const int fd = mkstemp(name.data());
      if (fd < 0) return failed();
      temp_ = std::move(name);
      if (exists) fchmod(fd, st.st_mode & 0777);
      file_ = fdopen(fd, "wb");
      if (!file_) close(fd);
    }
    if (!file_) return failed();
    return ms::io::write_wav_header(file_, frames, static_cast<int>(channels_), rate) || failed();
  }

  void sample(size_t i, float left, float right) {
    samples_[i * channels_] = left;
    samples_[(i + 1) * channels_ - 1] = right;
    if (stats_) {
      total_.add(left, right);
      window_.add(left, right);
    }
  }

  bool flush(size_t n, bool window_end) {
    if (stats_ && window_end) {
      windows_.push_back(window_);
      window_ = {};
    }
    const size_t count = n * channels_;
    return std::fwrite(samples_.data(), sizeof(float), count, file_) == count || failed();
  }

  bool publish() {
    const int status = std::fclose(file_);
    file_ = nullptr;
    if (status != 0 || (!temp_.empty() && std::rename(temp_.c_str(), path_.c_str()) != 0)) return failed();
    temp_.clear();
    return true;
  }

  const char* path() const { return path_.c_str(); }
  void print_stats(size_t frames, size_t win, const RenderOpts& o) const {
    if (!stats_) return;
    const double n = 2.0 * static_cast<double>(frames);
    std::printf("peak %6.1f dBFS   rms %6.1f dBFS   dc %+.5f\n",
                db(total_.peak), db(std::sqrt(total_.sumsq / n)), total_.dc / n);
    size_t start = 0;
    for (const Level& w : windows_) {
      const size_t len = std::min(win, frames - start);
      const double rms = db(std::sqrt(w.sumsq / (2.0 * static_cast<double>(len))));
      const int fill = static_cast<int>(std::clamp((rms + 60.0) / 60.0, 0.0, 1.0) * 20.0);
      std::printf("%7.1fs  rms %6.1f  peak %6.1f  |%s|\n",
                  o.start_sec + static_cast<double>(start) / o.rate, rms, db(w.peak), meter(fill).c_str());
      start += len;
    }
  }

 private:
  bool failed() const {
    std::fprintf(stderr, "error: cannot write '%s'\n", path_.c_str());
    return false;
  }

  std::string path_, temp_;
  size_t channels_;
  bool stats_;
  std::vector<float> samples_;
  std::FILE* file_ = nullptr;
  Level total_, window_;
  std::vector<Level> windows_;
};

bool render_blocks(ms::engine::Engine& engine, const RenderOpts& o, size_t frames, size_t win,
                   std::deque<RenderFile>& files) {
  std::array<float, kRenderBlock> l{}, r{};
  for (size_t start = 0; start < frames;) {
    const size_t n = std::min({kRenderBlock, frames - start, win - start % win});
    if (o.taps.empty()) engine.process(l.data(), r.data(), n);
    for (size_t i = 0; i < n; ++i) {
      if (!o.taps.empty()) engine.process(&l[i], &r[i], 1);
      files.front().sample(i, l[i], r[i]);
      for (size_t t = 0; t < o.taps.size(); ++t) {
        const auto& nodes = o.taps[t].nodes;
        files[t + 1].sample(i, engine.output_value(static_cast<size_t>(nodes.front()), 0),
                               engine.output_value(static_cast<size_t>(nodes.back()), 0));
      }
    }
    start += n;
    for (auto& f : files)
      if (!f.flush(n, start == frames || start % win == 0)) return false;
  }
  return true;
}

bool render_files(ms::engine::Engine& engine, const RenderOpts& o, size_t frames, size_t win) {
  std::deque<RenderFile> files;
  files.emplace_back(o.out_path, 2, o.stats);
  for (const auto& t : o.taps) {
    auto path = std::filesystem::path(o.out_path).replace_extension("." + t.name + ".wav");
    files.emplace_back(path.string(), t.nodes.size(), true);
  }
  for (auto& f : files)
    if (!f.open(frames, static_cast<int>(o.rate))) return false;

  if (!render_blocks(engine, o, frames, win, files)) return false;
  for (auto& f : files)
    if (!f.publish()) return false;
  std::printf("rendered %.1fs", o.seconds);
  if (o.start_sec > 0.0) std::printf(" from %.1fs", o.start_sec);
  std::printf(" -> %s\n", o.out_path.c_str());
  files.front().print_stats(frames, win, o);
  for (size_t t = 0; t < o.taps.size(); ++t) {
    const auto& tap = o.taps[t];
    std::printf("\ntap %s = %s (%s) -> %s\n", tap.name.c_str(), tap.endpoint.c_str(),
                tap.stereo ? "stereo" : "mono", files[t + 1].path());
    files[t + 1].print_stats(frames, win, o);
  }
  return true;
}

int cmd_render(const std::string& patch, int argc, char** argv) {
  RenderOpts o;
  if (!parse_render(argc, argv, o)) return usage();
  // Negated so a NaN (an unparsable duration) fails too.
  if (!(o.seconds > 0.0)) { std::fprintf(stderr, "error: -d needs a positive duration (90, 90s, 500ms, 2m)\n"); return 2; }
  if (!(o.start_sec >= 0.0)) { std::fprintf(stderr, "error: --start needs a duration >= 0 (90, 90s, 500ms, 2m)\n"); return 2; }

  const std::string bad_tap = ms::graph::check_taps(o.taps);
  if (!bad_tap.empty()) { std::fprintf(stderr, "error: %s\n", bad_tap.c_str()); return 2; }

  std::string src;
  ms::graph::PatchBuild b;
  if (!build(patch, o.rate, src, b)) return 1;
  const std::string tap_err = ms::graph::add_taps(b, src, patch, o.rate, o.taps);
  if (!tap_err.empty()) { std::fprintf(stderr, "error: %s\n", tap_err.c_str()); return 1; }
  ms::engine::Engine engine(take_graph(patch, b));

  const double count = o.seconds * o.rate;
  if (!(count >= 1 && count <= (UINT32_MAX - 36u) / 8u)) {
    std::fputs("error: duration must fit at least one frame and a stereo RIFF WAV\n", stderr);
    return 2;
  }
  if (!(o.start_sec * o.rate < static_cast<double>(std::numeric_limits<size_t>::max()))) {
    std::fputs("error: --start exceeds the supported frame range\n", stderr);
    return 2;
  }
  const size_t frames = static_cast<size_t>(count);
  const size_t win = static_cast<size_t>(std::clamp(o.stats_window * o.rate, 1.0, count));
  ms::rt::flush_denormals_on_this_thread();
  skip_frames(engine, static_cast<size_t>(o.start_sec * o.rate));
  const bool rendered = render_files(engine, o, frames, win);
  return !(engine.finish() && rendered);
}

// ---- play -------------------------------------------------------------------

std::atomic<bool> g_stop{false};
void on_sigint(int) { g_stop.store(true); }

void render_trampoline(void* user, float* l, float* r, size_t frames, uint64_t now_ns) {
  static_cast<ms::engine::RtRunner*>(user)->render(l, r, frames, now_ns);
}

void print_status(ms::engine::RtRunner& run,
                  const ms::engine::PipewireOut& pw,
                  bool watching, bool available) {
  auto bar = [](float v) {
    return meter(static_cast<int>(std::clamp(v, 0.0f, 1.0f) * 20.0f));
  };
  std::string line = "\r  L |" + bar(run.peak_l()) + "|  R |" + bar(run.peak_r()) +
                     "|  xruns " + std::to_string(pw.xruns());
  if (watching) {
    float v = 0.0f, last = 0.0f;
    bool got = false;
    while (run.try_watch_value(v)) { last = v; got = true; }
    if (!available) line += "  watch=unavailable";
    else if (got) {
      char buf[32];
      std::snprintf(buf, sizeof(buf), "  watch=% .3f", static_cast<double>(last));
      line += buf;
    }
  }
  if (isatty(fileno(stderr))) line += "\033[K";
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

  // Waits ~100 ms for a save, then reloads if one arrived. With no new save,
  // applies an edit held back while an earlier reload was crossfading.
  void poll(ms::engine::Controller& ctl) {
    if (!watching_) {
      // poll_changed() would return immediately instead of blocking, so
      // without this the loop spins at full CPU. Sleep to keep the same
      // ~10 Hz cadence as the watcher-enabled path.
      std::this_thread::sleep_for(std::chrono::milliseconds(100));
      return;
    }
    if (watcher_->poll_changed(100)) reload(ctl);
    else if (auto res = ctl.apply_pending()) finish(*res);
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
    if (!ms::graph::read_text_file(patch_, src)) {
      std::fprintf(stderr, "\nerror: cannot re-read '%s'\n", patch_.c_str());
      return;
    }
    finish(ctl.load(src, patch_));
  }

  void finish(const ms::engine::Controller::LoadResult& res) {
    if (res.deferred) {
      std::fputs("\nqueued (previous reload still crossfading)\n", stderr);
      return;
    }
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
    else if (const char* v = option(argc, argv, i, "--rate")) { if (!rate_option(v, o.rate)) return false; }
    else if (const char* v = option(argc, argv, i, "--watch")) o.watch = v;
    else return bad_option(argv[i]);
  }
  return true;
}

int cmd_play(const std::string& patch, int argc, char** argv) {
  PlayOpts o;
  if (!parse_play(argc, argv, o)) return usage();
  std::string src;
  if (!ms::graph::read_text_file(patch, src)) {
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
    print_status(runner, out.pw(), o.watch != nullptr, ctl.watch_available());
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
