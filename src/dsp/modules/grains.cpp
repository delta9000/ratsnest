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

    const std::string file = cfg_str(cfg, "file", "");
    file_mode_ = !file.empty();
    if (file_mode_) {
      // Static sample material: mono mixdown, linear resample to the engine
      // rate, 60 s cap. A missing/unreadable file stays a silent buffer
      // (design rule 6: errors never kill the session).
      std::vector<float> mono;
      io::WavData w;
      if (io::read_wav_f32(file, w) && w.frames > 0 && w.channels > 0) {
        const auto ch = static_cast<std::size_t>(w.channels);
        std::vector<float> mixed(w.frames);
        for (std::size_t fr = 0; fr < w.frames; ++fr) {
          float acc = 0.0f;
          for (std::size_t c = 0; c < ch; ++c) acc += w.samples[fr * ch + c];
          mixed[fr] = acc / static_cast<float>(ch);
        }
        const double step = static_cast<double>(w.sample_rate) / rate;
        const auto out_n = std::min(
            static_cast<std::size_t>(static_cast<double>(w.frames) / step),
            static_cast<std::size_t>(60.0 * rate));
        mono.resize(out_n);
        for (std::size_t i = 0; i < out_n; ++i) {
          const double p = static_cast<double>(i) * step;
          const auto i0 = static_cast<std::size_t>(p);
          const float f = static_cast<float>(p - static_cast<double>(i0));
          const float a = mixed[i0];
          const float b = (i0 + 1 < w.frames) ? mixed[i0 + 1] : a;
          mono[i] = a + (b - a) * f;
        }
      }
      if (mono.size() < 4096) mono.resize(4096, 0.0f);  // silent floor
      len_ = mono.size();
      buf_ = std::move(mono);
      mask_ = 0;       // unused: file reads clamp instead of wrapping
      write_abs_ = 0;  // no record head
    } else {
      const auto need = static_cast<std::size_t>(secs * rate);
      std::size_t size = 1;
      while (size < need) size <<= 1;
      buf_.assign(size, 0.0f);  // control thread: allocation is legal here
      mask_ = size - 1;
      len_ = size;
      write_abs_ = size;  // one ring in: read math never goes negative
    }

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
    const float trig = sanitize(ins[2]);

    record(in, frozen);

    // External trig: exactly one grain per rising edge, any density.
    bool seeded = false;
    if (prev_trig_ <= 0.5f && trig > 0.5f) seeded = seed_grain(ins);
    prev_trig_ = trig;
    seeded = schedule(ins) || seeded;

    float sum_l = 0.0f;
    float sum_r = 0.0f;
    int active = 0;
    for (Grain& g : grains_) {
      if (!g.active) continue;
      ++active;
      const float w = window(static_cast<float>(g.env_phase), g.shape);
      const float s = read(g.phase) * w;
      sum_l += s * g.gain_l;
      sum_r += s * g.gain_r;
      g.phase += g.inc;
      g.env_phase += g.env_inc;
      if (g.env_phase >= 1.0) g.active = false;
    }

    // Loudness normalization: grain count + window energy (Clouds), smoothed
    // ~10 ms so births/deaths don't step the gain.
    const float shape_now = sanitize_clamp(ins[9], 0.0f, 1.0f, 0.5f);
    const float target = std::min(1.0f + 2.0f * shape_now, 2.0f) /
                         std::sqrt(static_cast<float>(std::max(active, 1)));
    norm_ += norm_a_ * (target - norm_);
    norm_ = flush_denormal(norm_);

    outs[0] = sum_l * norm_;
    outs[1] = sum_r * norm_;
    outs[2] = seeded ? 1.0f : 0.0f;
  }

  void copy_state_from(const Module& other) override {
    const auto& o = static_cast<const Grains&>(other);
    // RT-called: bounded copy, no allocation (delay precedent; ~1 MB at the
    // default 4 s ring). Sizes match unless a secs edit resized the ring.
    const std::size_t n = std::min(buf_.size(), o.buf_.size());
    std::copy_n(o.buf_.begin(), n, buf_.begin());
    write_abs_ = o.write_abs_;
    prev_trig_ = o.prev_trig_;
    was_frozen_ = o.was_frozen_;
    tail_ = o.tail_;
    tail_len_ = o.tail_len_;
    xfade_pos_ = o.xfade_pos_;
    rng_ = o.rng_;
    countdown_ = o.countdown_;
    norm_ = o.norm_;
    grains_ = o.grains_;
    // A resize may have shrunk the ring: kill grains that no longer fit.
    for (Grain& g : grains_) {
      if (!g.active) continue;
      if (file_mode_) {
        if (g.phase >= static_cast<double>(len_)) g.active = false;
      } else if (static_cast<double>(write_abs_) - g.phase >
                 static_cast<double>(len_) - 8.0) {
        g.active = false;
      }
    }
  }

 private:
  // Advance the record head. While frozen, capture the first kTail inputs
  // (continuous with the frozen content's edge); on unfreeze, crossfade the
  // written stream from that tail into live input — Clouds' click-free
  // WriteFade resume (spec §3 "Freeze").
  void record(float in, bool frozen) {
    if (file_mode_) return;
    if (frozen) {
      if (!was_frozen_) {
        tail_len_ = 0;
        was_frozen_ = true;
      }
      if (tail_len_ < kTail) tail_[static_cast<std::size_t>(tail_len_++)] = in;
      return;
    }
    if (was_frozen_) {
      xfade_pos_ = 0;
      was_frozen_ = false;
    }
    float x = in;
    if (xfade_pos_ < kTail) {
      if (tail_len_ > 0) {
        const float a = static_cast<float>(xfade_pos_) / static_cast<float>(kTail);
        const int ti = std::min(xfade_pos_, tail_len_ - 1);
        x = tail_[static_cast<std::size_t>(ti)] * (1.0f - a) + in * a;
      }
      ++xfade_pos_;
    }
    buf_[static_cast<std::size_t>(write_abs_) & mask_] = x;
    ++write_abs_;
  }

  // Internal clock (spec §3 "Scheduling"): after each internal seed the next
  // onset is drawn as dt = base * ((1 - jitter) + jitter * (-ln u)). Since
  // E[-ln u] = 1, the mean rate is exactly `density` for every jitter;
  // jitter 0 is a synchronous stream, jitter 1 a Poisson cloud.
  bool schedule(const float* ins) {
    const float density = sanitize_clamp(ins[7], 0.0f, 200.0f);
    if (density <= 0.0f) return false;
    bool seeded = false;
    if (countdown_ <= 0.0) {
      seeded = seed_grain(ins);
      const float jitter = sanitize_clamp(ins[8], 0.0f, 1.0f, 0.5f);
      const double base =
          static_cast<double>(rate_) / static_cast<double>(density);
      const double u = 1.0 - static_cast<double>(frand());  // (0, 1]
      const double dt = base * ((1.0 - static_cast<double>(jitter)) +
                                static_cast<double>(jitter) * -std::log(u));
      countdown_ += std::max(dt, 1.0);
    }
    countdown_ -= 1.0;
    return seeded;
  }

  // Latch one grain from the current input values. Returns false when the
  // pool is saturated (seeds drop silently, like the hardware).
  bool seed_grain(const float* ins) {
    Grain* g = nullptr;
    for (Grain& c : grains_) {
      if (!c.active) {
        g = &c;
        break;
      }
    }
    if (g == nullptr) return false;

    const float size_s = sanitize_clamp(ins[5], 0.005f, 2.0f, 0.1f);
    const float pitch = sanitize_clamp(ins[6], -4.0f, 4.0f);
    const float shape = sanitize_clamp(ins[9], 0.0f, 1.0f, 0.5f);
    const float spread = sanitize_clamp(ins[10], 0.0f, 1.0f, 0.5f);
    const bool reverse = sanitize(ins[11]) > 0.5f;
    float pos = sanitize_clamp(ins[3], 0.0f, 1.0f, 0.5f);
    const float scatter = sanitize_clamp(ins[4], 0.0f, 1.0f);
    if (scatter > 0.0f) {  // draw only when active: keeps prior seeds' RNG sequence
      pos = std::clamp(pos + scatter * (frand() - 0.5f), 0.0f, 1.0f);
    }

    const double len = static_cast<double>(len_);
    const double ratio = std::exp2(static_cast<double>(pitch));
    double dur = static_cast<double>(size_s) * static_cast<double>(rate_);
    double span = dur * std::max(ratio, 1.0);  // buffer distance covered
    if (span > len * 0.25) {  // keep every grain inside a quarter ring
      dur *= (len * 0.25) / span;
      span = len * 0.25;
    }

    // Placement (spec §3): margins guarantee the read head never crosses the
    // moving record head nor out-runs the oldest sample.
    double start;
    if (file_mode_) {
      start = 4.0 + static_cast<double>(pos) * std::max(len - span - 8.0, 0.0);
    } else {
      const double available = std::max(len - 2.0 * span - 8.0, 0.0);
      const double delay = static_cast<double>(pos) * available + span + 4.0;
      start = static_cast<double>(write_abs_) - delay;
    }

    g->active = true;
    g->phase = reverse ? start + span : start;
    g->inc = reverse ? -ratio : ratio;
    g->env_phase = 0.0;
    g->env_inc = 1.0 / std::max(dur, 1.0);
    g->shape = shape;
    const float pan = 0.5f + spread * (frand() - 0.5f);
    const float angle = pan * (std::numbers::pi_v<float> / 2.0f);
    g->gain_l = std::cos(angle);
    g->gain_r = std::sin(angle);
    return true;
  }

  // Window morph (Clouds Grain::RenderEnvelope): folded triangle; below 0.5
  // the slopes steepen toward a boxcar, above 0.5 it blends toward a
  // raised-cosine of the triangle -> Hann at 1.
  static float window(float p, float shape) {
    const float tri = 1.0f - std::abs(2.0f * p - 1.0f);
    if (shape < 0.5f) {
      return std::min(tri * (0.5f / (shape + 0.01f)), 1.0f);
    }
    const float rc = 0.5f - 0.5f * std::cos(std::numbers::pi_v<float> * tri);
    return tri + (2.0f * shape - 1.0f) * (rc - tri);
  }

  // 4-point cubic Hermite read. Live: masked ring indexing (branch-free
  // wrap). File: clamped so the stencil stays inside [0, len).
  float read(double idx) const {
    if (file_mode_) {
      const double c =
          std::clamp(idx, 1.0, static_cast<double>(len_) - 3.0);
      const auto i1 = static_cast<std::size_t>(c);
      const float f = static_cast<float>(c - static_cast<double>(i1));
      return hermite(buf_[i1 - 1], buf_[i1], buf_[i1 + 1], buf_[i1 + 2], f);
    }
    const auto i1 = static_cast<uint64_t>(idx);
    const float f = static_cast<float>(idx - static_cast<double>(i1));
    const float x0 = buf_[static_cast<std::size_t>(i1 - 1) & mask_];
    const float x1 = buf_[static_cast<std::size_t>(i1) & mask_];
    const float x2 = buf_[static_cast<std::size_t>(i1 + 1) & mask_];
    const float x3 = buf_[static_cast<std::size_t>(i1 + 2) & mask_];
    return hermite(x0, x1, x2, x3, f);
  }

  static float hermite(float x0, float x1, float x2, float x3, float f) {
    const float c1 = 0.5f * (x2 - x0);
    const float c2 = x0 - 2.5f * x1 + 2.0f * x2 - 0.5f * x3;
    const float c3 = 0.5f * (x3 - x0) + 1.5f * (x1 - x2);
    return ((c3 * f + c2) * f + c1) * f + x1;
  }

  // Marsaglia xorshift32; see docs/attribution.md. Deterministic per seed.
  float frand() {
    rng_ ^= rng_ << 13;
    rng_ ^= rng_ >> 17;
    rng_ ^= rng_ << 5;
    return static_cast<float>(rng_ >> 8) * (1.0f / 16777216.0f);
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
