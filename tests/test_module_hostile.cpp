// Registry-driven robustness sweep: every module, every input, every hostile
// value. The engine zeroes non-finite outputs, so a module may pass a NaN
// straight through while it is being fed one (a stateless `math` add has no
// choice). What it must never do is keep that NaN: once hostile input stops
// and inputs return to their defaults, every output has to be finite again,
// immediately and for the rest of the run, including after a hot-reload
// style copy_state_from into a fresh instance. A poisoned filter, phase
// accumulator, envelope or delay line would otherwise stay silent (zeroed)
// until the patch is reloaded.
//
// Coverage notes / skips:
//   * Modules run with their default config, then once per enum config value
//     (every wave, model, mode...). Modules with a `file` config (sampler,
//     grains) are also swept with a short WAV loaded, since an empty buffer
//     skips most of their state; `tracker` is also swept with a song `text`. `rec` is never given a file: its destructor
//     would write it. Numeric configs keep their defaults.
//   * on_midi / on_audio_in payloads are not swept: `midi` decodes bytes into
//     discrete state (no float arithmetic), and `extin` is a pure pass-through
//     ring whose contents drain within one ring length.
#include <cmath>
#include <cstdio>
#include <filesystem>
#include <limits>
#include <memory>
#include <string>
#include <vector>
#include <doctest/doctest.h>
#include "dsp/module.h"
#include "io/wav.h"

using namespace ms::dsp;

namespace {

constexpr double kRate = 48000.0;
constexpr int kWarm = 256;     // settle before the hostile burst
constexpr int kBurst = 64;     // hostile samples on one input
constexpr int kRecover = 24000; // 0.5 s of default input afterwards: longer
                                // than delay's default time, so a poisoned
                                // delay line would replay inside the window
constexpr int kRecoverShort = 4800;  // enum variants: state, not long lines

struct Rig {
  std::unique_ptr<Module> m;
  std::vector<float> ins, outs;
  Rig(const ModuleInfo& info, const Config& cfg) : m(info.create()) {
    m->init(kRate, cfg);
    for (const auto& in : info.inputs) ins.push_back(in.def);
    outs.assign(info.outputs.empty() ? 1 : info.outputs.size(), 0.0f);
  }
  // Runs n samples; returns the index of the first sample with a non-finite
  // output, or -1.
  int run(int n) {
    int bad = -1;
    for (int s = 0; s < n; ++s) {
      m->process(ins.data(), outs.data());
      for (float o : outs)
        if (!std::isfinite(o) && bad < 0) bad = s;
    }
    return bad;
  }
};

std::string short_wav() {
  const auto path = std::filesystem::temp_directory_path() / "ms_hostile.wav";
  std::vector<float> data(4800);
  for (size_t i = 0; i < data.size(); ++i)
    data[i] = 0.5f * std::sin(static_cast<float>(i) * 0.05f);
  REQUIRE(ms::io::write_wav_f32(path.string(), data.data(), data.size(), 1,
                                static_cast<int>(kRate)));
  return path.string();
}

// A small song for the tracker: notes, slurs, offs, CVs, layers, a loop.
const char* const kTrkFixture =
    "track a note vel cva cvb\ntrack b note\n"
    "pattern p 4 a\n| C4 .5 1 2\n| ~D4\n| off\n| x\n"
    "pattern q 2 b\n| x\n| x\n"
    "order p |: p+q*2\n";

std::string fmt(float v) {
  char buf[32];
  std::snprintf(buf, sizeof buf, "%g", static_cast<double>(v));
  return buf;
}

void sweep(const ModuleInfo& info, const Config& cfg, const std::string& label,
           int recover = kRecover) {
  const float hostile[] = {std::numeric_limits<float>::quiet_NaN(),
                           std::numeric_limits<float>::infinity(),
                           -std::numeric_limits<float>::infinity(),
                           1e30f, -1e30f, 1e-40f /* denormal */};
  for (size_t i = 0; i < info.inputs.size(); ++i) {
    for (float v : hostile) {
      Rig a(info, cfg);
      a.run(kWarm);
      a.ins[i] = v;
      a.run(kBurst);
      a.ins[i] = info.inputs[i].def;

      // Hot-reload shape: a fresh instance adopts the burst-time state.
      Rig b(info, cfg);
      b.m->copy_state_from(*a.m);

      const int bad_a = a.run(recover);
      const int bad_b = b.run(recover);
      const std::string where = std::string(info.type) + label + "." +
                                info.inputs[i].name + " = " + fmt(v);
      if (bad_a >= 0)
        FAIL_CHECK(where << ": non-finite output " << bad_a
                         << " samples after the burst");
      if (bad_b >= 0)
        FAIL_CHECK(where << ": copy_state_from target non-finite "
                         << bad_b << " samples after the burst");
    }
  }
  // Copying between two clean instances must also stay finite.
  Rig a(info, cfg), b(info, cfg);
  a.run(kWarm);
  b.m->copy_state_from(*a.m);
  const std::string what = std::string(info.type) + label;
  CHECK_MESSAGE(b.run(recover) < 0, what << ": copy_state_from (clean)");
}

}  // namespace

TEST_CASE("every module recovers from hostile input") {
  const std::string wav = short_wav();
  for (const ModuleInfo* info : all_modules()) {
    sweep(*info, {}, "");
    Config base;
    if (info->config("file") != nullptr && std::string(info->type) != "rec") {
      base.emplace_back("file", ConfigValue{wav, 0.0, {}, false});
      sweep(*info, base, "[file]");
    }
    // The tracker's song arrives as `text` (the loader embeds the .trk).
    if (info->config("text") != nullptr) {
      Config song;
      song.emplace_back("text", ConfigValue{kTrkFixture, 0.0, {}, false});
      sweep(*info, song, "[text]");
    }
    // Each enum value selects a different code path (wave, model, mode...).
    for (const ConfigInfo& c : info->configs) {
      if (c.enum_vals == nullptr) continue;
      for (const char* const* e = c.enum_vals; *e != nullptr; ++e) {
        Config cfg = base;
        cfg.emplace_back(c.name, ConfigValue{*e, 0.0, {}, false});
        sweep(*info, cfg, std::string("[") + c.name + "=" + *e + "]",
              kRecoverShort);
      }
    }
  }
  std::filesystem::remove(wav);
}
