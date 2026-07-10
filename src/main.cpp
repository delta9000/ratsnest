#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>
#include "dsl/parser.h"
#include "engine/engine.h"
#include "graph/graph.h"
#include "io/wav.h"

namespace {

int usage() {
  std::fprintf(stderr,
      "usage:\n"
      "  modsynth check  <patch>\n"
      "  modsynth render <patch> [-d <seconds|Ns>] [-o <out.wav>] [--rate <hz>]\n");
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

  return usage();
}
