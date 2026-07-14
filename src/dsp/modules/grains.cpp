#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <numbers>
#include <string>
#include <vector>
#include "dsp/mastering.h"
#include "dsp/modules.h"
#include "io/wav.h"

namespace ms::dsp {
namespace {

constexpr InputInfo kIns[] = {
    {"in", 0.f},      {"freeze", 0.f}, {"trig", 0.f},    {"position", 0.5f},
    {"scatter", 0.f}, {"size", 0.1f},  {"pitch", 0.f},   {"density", 10.f},
    {"jitter", 0.5f}, {"shape", 0.5f}, {"spread", 0.5f}, {"reverse", 0.f}};
constexpr ConfigInfo kCfg[] = {
    {"secs", nullptr}, {"file", nullptr}, {"seed", nullptr}};
constexpr OutputInfo kOuts[] = {{"out_l"}, {"out_r"}, {"onset"}};

constexpr int kMaxGrains = 64;  // fixed pool, like the Clouds firmware
constexpr int kTail = 256;      // unfreeze crossfade length (~5 ms @ 48 kHz)

// One replay voice. All fields latch at onset (spec §3 "Grain pool");
// trivially copyable so the whole pool migrates with std::array assignment.
struct Grain {
  bool active = false;
  double phase = 0.0;      // absolute sample index (live) / buffer index (file)
  double inc = 1.0;        // signed read increment; negative = reverse
  double env_phase = 0.0;  // 0..1 across the grain
  double env_inc = 0.0;
  float shape = 0.5f;
  float gain_l = 0.0f;
  float gain_r = 0.0f;
};

class Grains final : public Module {
 public:
  void init(double rate, const Config& cfg) override {
    rate_ = static_cast<float>(rate);
    const double secs = std::clamp(cfg_num(cfg, "secs", 4.0), 0.25, 60.0);
    const auto seed = static_cast<uint32_t>(cfg_num(cfg, "seed", 1.0));
    rng_ = seed ? seed : 1u;  // xorshift must never hold zero
    file_mode_ = false;       // the file task replaces this init

    const auto need = static_cast<std::size_t>(secs * rate);
    std::size_t size = 1;
    while (size < need) size <<= 1;
    buf_.assign(size, 0.0f);  // control thread: allocation is legal here
    mask_ = size - 1;
    len_ = size;
    write_abs_ = size;  // start one ring in: read math never goes negative

    grains_ = {};
    tail_.fill(0.0f);
    tail_len_ = 0;
    xfade_pos_ = kTail;
    was_frozen_ = false;
    prev_trig_ = 0.0f;
    countdown_ = 0.0;
    norm_ = 1.0f;
    norm_a_ = 1.0f - std::exp(-1.0f / (0.010f * rate_));
  }

  void process(const float* ins, float* outs) override {
    const float in = sanitize(ins[0]);
    const bool frozen = sanitize(ins[1]) > 0.5f;
    record(in, frozen);
    outs[0] = 0.0f;
    outs[1] = 0.0f;
    outs[2] = 0.0f;
  }

 private:
  // Advance the record head by one sample. The freeze-tail crossfade task
  // replaces this with the click-free version.
  void record(float in, bool frozen) {
    if (file_mode_ || frozen) return;
    buf_[static_cast<std::size_t>(write_abs_) & mask_] = in;
    ++write_abs_;
  }

  float rate_ = 48000.0f;
  bool file_mode_ = false;
  std::vector<float> buf_;
  std::size_t mask_ = 0;
  std::size_t len_ = 0;
  uint64_t write_abs_ = 0;
  std::array<Grain, kMaxGrains> grains_{};
  std::array<float, kTail> tail_{};
  int tail_len_ = 0;
  int xfade_pos_ = kTail;
  bool was_frozen_ = false;
  float prev_trig_ = 0.0f;
  double countdown_ = 0.0;
  uint32_t rng_ = 1;
  float norm_ = 1.0f;
  float norm_a_ = 0.0f;
};

std::unique_ptr<Module> make() { return std::make_unique<Grains>(); }

}  // namespace

const ModuleInfo grains_info{"grains", kIns, kCfg, kOuts, &make};

}  // namespace ms::dsp
