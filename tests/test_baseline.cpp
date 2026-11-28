// Render baseline: every patch in the repo, rendered and fingerprinted, must
// match tests/data/render-baseline.txt. This is the net under "bit-identical"
// claims: a change that moves any patch's sound fails here, and a change that
// means to move it rewrites the baseline in the same commit:
//
//     RATSNEST_UPDATE_BASELINE=1 ./build/mstests -tc='render baseline*'
//     (or: mise run baseline)
//
// The fingerprint is levels, not a hash: the build runs on several libm
// versions (glibc moved float math to CORE-MATH in 2.44), so last-bit
// differences are expected across machines and a hash would fail on every
// one of them. Per one-second window, per channel: RMS, and RMS of the first
// difference (a brightness reading, which moves when the timbre does even at
// the same level). Plus whole-take peak, true peak and integrated loudness.
// Every value is in dB and compared to kTol.
//
// Patches that load a CLAP `plugin` are skipped: their sound depends on what
// is installed on the machine, and in CI nothing is (they run silent).
#include <doctest/doctest.h>
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <map>
#include <sstream>
#include <string>
#include <thread>
#include <vector>
#include "engine/engine.h"
#include "graph/subs.h"
#include "io/loudness.h"

namespace {

const auto kRoot = std::filesystem::path(__FILE__).parent_path().parent_path();
const auto kBaseline = kRoot / "tests" / "data" / "render-baseline.txt";
constexpr double kRate = 48000.0;
constexpr int kSeconds = 8;
constexpr double kTol = 0.2;     // dB
constexpr double kFloor = -100;  // dB; below this every reading is "silent"

double db(double v) { return std::max(kFloor, 20.0 * std::log10(std::max(v, 1e-12))); }

std::string read_file(const std::filesystem::path& p) {
  std::ifstream f(p);
  std::stringstream ss;
  ss << f.rdbuf();
  return ss.str();
}

// The patches the baseline covers, as paths relative to the repo root.
std::vector<std::string> baseline_patches() {
  std::vector<std::string> out;
  for (const auto& e : std::filesystem::recursive_directory_iterator(kRoot / "patches")) {
    if (!e.is_regular_file() || e.path().extension() != ".patch") continue;
    if (read_file(e.path()).find("plugin {") != std::string::npos) continue;
    out.push_back(e.path().lexically_relative(kRoot).generic_string());
  }
  std::sort(out.begin(), out.end());
  return out;
}

// One patch's fingerprint, every value in dB.
std::vector<double> fingerprint(const std::string& rel, double rate, int seconds) {
  const auto path = kRoot / rel;
  auto b = ms::graph::build_patch(read_file(path), path.string(), rate);
  if (!b.compiled.ok()) return {};
  ms::engine::Engine e(std::move(b.compiled.graph));
  const size_t n = static_cast<size_t>(rate) * static_cast<size_t>(seconds);
  std::vector<float> l(n), r(n);
  e.process(l.data(), r.data(), n);

  const auto lu = ms::io::measure_loudness(l.data(), r.data(), n, rate);
  double peak = 0.0;
  for (size_t i = 0; i < n; ++i) peak = std::max({peak, double(std::abs(l[i])), double(std::abs(r[i]))});
  std::vector<double> fp{std::max(kFloor, lu.integrated), std::max(kFloor, lu.true_peak), db(peak)};
  const size_t win = static_cast<size_t>(rate);
  for (const auto* ch : {&l, &r}) {
    for (size_t s = 0; s < n; s += win) {
      double sq = 0.0, dsq = 0.0;
      for (size_t i = s; i < s + win; ++i) {
        const double x = (*ch)[i], d = i > 0 ? x - (*ch)[i - 1] : 0.0;
        sq += x * x;
        dsq += d * d;
      }
      fp.push_back(db(std::sqrt(sq / double(win))));
      fp.push_back(db(std::sqrt(dsq / double(win))));
    }
  }
  return fp;
}

std::map<std::string, std::vector<double>> read_baseline() {
  std::map<std::string, std::vector<double>> m;
  std::ifstream f(kBaseline);
  std::string line;
  while (std::getline(f, line)) {
    if (line.empty() || line[0] == '#') continue;
    std::istringstream ss(line);
    std::string name;
    ss >> name;
    std::vector<double> v;
    for (double x; ss >> x;) v.push_back(x);
    m[name] = v;
  }
  return m;
}

// Fingerprints of every patch, rendered on all cores (each engine is
// independent, and the order of results follows `patches`).
std::vector<std::vector<double>> fingerprints(const std::vector<std::string>& patches,
                                              double rate, int seconds) {
  std::vector<std::vector<double>> out(patches.size());
  const size_t workers = std::max(1u, std::thread::hardware_concurrency());
  std::vector<std::thread> pool;
  for (size_t w = 0; w < workers; ++w)
    pool.emplace_back([&, w] {
      for (size_t i = w; i < patches.size(); i += workers)
        out[i] = fingerprint(patches[i], rate, seconds);
    });
  for (auto& t : pool) t.join();
  return out;
}

}  // namespace

TEST_CASE("render baseline: every patch still sounds the way it did") {
  const auto patches = baseline_patches();
  REQUIRE(patches.size() > 50);
  const auto fps = fingerprints(patches, kRate, kSeconds);
  if (const char* u = std::getenv("RATSNEST_UPDATE_BASELINE"); u != nullptr && *u == '1') {
    std::ofstream f(kBaseline);
    f << "# tests/test_baseline.cpp: " << kSeconds << " s at 48 kHz. Per patch: LUFS,\n"
         "# true peak, peak, then per channel per second: rms, rms of the first\n"
         "# difference. All dB. Regenerate with `mise run baseline`.\n";
    for (size_t i = 0; i < patches.size(); ++i) {
      f << patches[i];
      char buf[16];
      for (double v : fps[i]) {
        std::snprintf(buf, sizeof buf, " %.2f", v);
        f << buf;
      }
      f << '\n';
    }
    MESSAGE("rewrote " << kBaseline.string());
    return;
  }
  const auto base = read_baseline();
  for (size_t k = 0; k < patches.size(); ++k) {
    const auto& p = patches[k];
    CAPTURE(p);
    const auto it = base.find(p);
    // A new patch joins the baseline in the commit that adds it.
    REQUIRE_MESSAGE(it != base.end(), "not in the baseline; run `mise run baseline`");
    const auto& fp = fps[k];
    REQUIRE(fp.size() == it->second.size());
    double worst = 0.0;
    size_t at = 0;
    for (size_t i = 0; i < fp.size(); ++i)
      if (std::abs(fp[i] - it->second[i]) > worst) worst = std::abs(fp[i] - it->second[i]), at = i;
    CAPTURE(at);
    CHECK(worst <= kTol);
  }
  for (const auto& [name, v] : base) {
    CAPTURE(name);
    CHECK_MESSAGE(std::find(patches.begin(), patches.end(), name) != patches.end(),
                  "in the baseline but no longer a patch; run `mise run baseline`");
  }
}
