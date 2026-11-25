#include <algorithm>
#include <cmath>
#include <string>
#include <vector>
#include "dsp/modules.h"
#include "dsp/util.h"
#include "io/wav.h"

namespace ms::dsp {
namespace {

constexpr InputInfo kIns[] = {
    {"trig", 0.0f},
    {"pitch", 0.0f},
    {"level", 1.0f},
};
constexpr const char* kLoop[] = {"off", "on", nullptr};
constexpr ConfigInfo kCfg[] = {
    {"file", nullptr, ConfigKind::Text},   // arbitrary path string (quoted in the patch)
    {"loop", kLoop},
};
// Retrigger crossfade and loop-seam / end-of-file slew, seconds.
constexpr float kFade = 0.005f;
constexpr OutputInfo kOuts[] = {
    {"out"},
    {"eos"},
};

class Sampler final : public Module {
 public:
  void init(double rate, const Config& cfg) override {
    engine_rate_ = static_cast<float>(rate);
    loop_ = (cfg_str(cfg, "loop", "off") == "on");
    mips_.clear();
    len_ = 0;
    file_rate_ = engine_rate_;

    const std::string file = cfg_str(cfg, "file", "");
    if (!file.empty()) {
      io::WavData w;
      if (io::read_wav_f32(file, w) && w.frames > 0 && w.channels > 0) {
        // Mix any channel count down to a single mono buffer.
        std::vector<float> mono(w.frames);
        const size_t ch = static_cast<size_t>(w.channels);
        for (size_t fr = 0; fr < w.frames; ++fr) {
          float acc = 0.0f;
          for (size_t c = 0; c < ch; ++c) acc += w.samples[fr * ch + c];
          mono[fr] = acc / static_cast<float>(w.channels);
        }
        len_ = mono.size();
        file_rate_ = static_cast<float>(w.sample_rate);
        build_mips(std::move(mono));
      }
    }
    pos_ = 0.0;
    playing_ = len_ > 0;  // play once at load; trig can restart
    trig_edge_ = RisingEdge{};
    fade_ = kFade * engine_rate_;
    old_left_ = 0.0f;
    seam_ = false;
    last_ = 0.0f;
    seam_dc_ = Declick{};
  }

  void process(const float* ins, float* outs) override {
    const float trig = ins[0];
    // Clamped like svf/ladder v_oct: a non-finite or absurd pitch would make
    // pos_ non-finite, and casting that to an index is undefined.
    const float pitch = sanitize_clamp(ins[1], -24.0f, 24.0f);
    const float level = ins[2];
    const double step = static_cast<double>(file_rate_ / engine_rate_) *
                        std::pow(2.0, static_cast<double>(pitch));

    // Rising edge on trig restarts playback from the beginning at full
    // level; a voice still playing moves to the fade head and dies out.
    if (trig_edge_(trig)) {
      if (playing_) { old_pos_ = pos_; old_left_ = fade_; }
      seam_ = false;  // the fade head carries the old voice instead
      pos_ = 0.0;
      playing_ = len_ > 0;
    }

    float out = 0.0f;
    float eos = 0.0f;
    const double before = pos_;
    const Mip mip = pick(step);
    if (playing_) {
      out = read(pos_, mip);
      pos_ += step;
      playing_ = loop_ || pos_ < static_cast<double>(len_);  // one-shot ends
      eos = static_cast<float>(!playing_);
      pos_ = std::fmod(pos_, static_cast<double>(len_));
    }
    // A loop wrap or the end of the file is a seam: slew its step out.
    if (seam_) seam_dc_.jump(last_, out, fade_);
    seam_ = pos_ < before;
    last_ = out;
    out = seam_dc_(out);
    if (old_left_ > 0.0f) {  // fade head; holds the last frame at the end
      out += read(old_pos_, mip) * (old_left_ / fade_);
      old_left_ -= 1.0f;
      old_pos_ = std::min(old_pos_ + step, static_cast<double>(len_ - 1));
    }

    outs[0] = out * level;
    outs[1] = eos;
  }

  void copy_state_from(const Module& other) override {
    const auto& o = static_cast<const Sampler&>(other);
    pos_ = o.pos_;
    playing_ = o.playing_;
    trig_edge_ = o.trig_edge_;
    old_pos_ = std::min(o.old_pos_, static_cast<double>(len_) - 1.0);
    old_left_ = o.old_left_;
    seam_ = o.seam_;
    last_ = o.last_;
    seam_dc_ = o.seam_dc_;
    // A reload may have loaded a shorter buffer; keep the read positions valid.
    if (pos_ >= static_cast<double>(len_)) {
      pos_ = 0.0;
      playing_ = false;
      old_left_ = 0.0f;
    }
  }

 private:
  // Mip levels: level k is the file low-passed and decimated k times by
  // two, so reading it at step / 2^k keeps partials below Nyquist. Built
  // once at load (init may allocate); level 0 is the file itself. Each level
  // carries one guard frame before and two after for the 4-point read: the
  // other end's frames when looping, else the edge frame repeated.
  static constexpr int kMaxMips = 6;  // levels 0..5: clean up to +5 octaves
  static void guard(std::vector<float>& v, bool loop) {
    const size_t n = v.size();
    const float pre = loop ? v[n - 1] : v[0];
    const float post1 = loop ? v[0] : v[n - 1], post2 = loop ? v[n > 1 ? 1 : 0] : v[n - 1];
    v.insert(v.begin(), pre);
    v.push_back(post1);
    v.push_back(post2);
  }
  void build_mips(std::vector<float> file) {
    // Half-band low-pass: 63-tap Blackman-windowed sinc, cutoff 0.21 of the
    // level's rate, so the stopband begins near the decimated Nyquist.
    constexpr int kTaps = 63, kHalf = kTaps / 2;
    constexpr double kCut = 0.21, kPi = 3.14159265358979323846;
    double h[kTaps], sum = 0.0;
    for (int i = 0; i < kTaps; ++i) {
      const double t = i - kHalf;
      const double sinc = t == 0 ? 2.0 * kCut : std::sin(2.0 * kPi * kCut * t) / (kPi * t);
      const double w = 0.42 - 0.5 * std::cos(2.0 * kPi * i / (kTaps - 1)) +
                       0.08 * std::cos(4.0 * kPi * i / (kTaps - 1));
      sum += h[i] = sinc * w;
    }
    std::vector<float> src = std::move(file);
    for (int k = 0;; ++k) {
      const auto n = static_cast<std::ptrdiff_t>(src.size());
      std::vector<float> next;
      if (k + 1 < kMaxMips && n > 2 * kTaps) {
        next.resize(static_cast<size_t>((n + 1) / 2));
        for (std::ptrdiff_t j = 0; j < static_cast<std::ptrdiff_t>(next.size()); ++j) {
          double acc = 0.0;
          for (int i = 0; i < kTaps; ++i) {
            std::ptrdiff_t at = 2 * j + i - kHalf;  // wrap when looping, else hold
            at = loop_ ? ((at % n) + n) % n : std::clamp<std::ptrdiff_t>(at, 0, n - 1);
            acc += h[i] * static_cast<double>(src[static_cast<size_t>(at)]);
          }
          next[static_cast<size_t>(j)] = static_cast<float>(acc / sum);
        }
      }
      guard(src, loop_);
      mips_.push_back(std::move(src));
      if (next.empty()) return;
      src = std::move(next);
    }
  }
  // Which levels a step reads: up to unity, level 0 alone (the file
  // itself); above, an octave's crossfade from level k (step/2^k in [1, 2))
  // toward k + 1 as that step nears 2, so no level switch is audible.
  struct Mip { int k = 0; float mix = 0.0f; };
  Mip pick(double step) const {
    if (!(step > 1.0) || mips_.size() < 2) return {};
    const double oct = std::log2(step);
    const int top = static_cast<int>(mips_.size()) - 1;
    const int k = std::min(static_cast<int>(oct), top);
    return {k, k < top ? static_cast<float>(oct - k) : 0.0f};
  }
  // The sample at file position pos in [0, len_).
  float read(double pos, const Mip& m) const {
    const float a = read_level(m.k, pos);
    return m.mix > 0.0f ? a + m.mix * (read_level(m.k + 1, pos) - a) : a;
  }
  // 4-point, 3rd-order Hermite: exact on whole frames, and its images sit
  // far lower than linear interpolation's once a level plays sped up.
  float read_level(int k, double pos) const {
    const std::vector<float>& b = mips_[static_cast<size_t>(k)];
    const double p = std::ldexp(pos, -k);
    const size_t i = std::min(static_cast<size_t>(p), b.size() - 4);  // b[i+1] = frame i
    const float t = static_cast<float>(p - static_cast<double>(i));
    const float xm = b[i], x0 = b[i + 1], x1 = b[i + 2], x2 = b[i + 3];
    const float c1 = 0.5f * (x1 - xm);
    const float c2 = xm - 2.5f * x0 + 2.0f * x1 - 0.5f * x2;
    const float c3 = 0.5f * (x2 - xm) + 1.5f * (x0 - x1);
    return ((c3 * t + c2) * t + c1) * t + x0;
  }

  float engine_rate_ = 48000.0f;
  float file_rate_ = 48000.0f;
  bool loop_ = false;
  std::vector<std::vector<float>> mips_;  // level 0 = the file, len_ frames + guard
  size_t len_ = 0;
  double pos_ = 0.0;
  bool playing_ = false;
  RisingEdge trig_edge_;
  float fade_ = 0.0f;      // kFade in samples
  double old_pos_ = 0.0;   // fade head: the retriggered voice dying out
  float old_left_ = 0.0f;  // fade-head samples left
  bool seam_ = false;      // the main head wrapped or stopped last sample
  float last_ = 0.0f;      // main head's last raw output
  Declick seam_dc_;
};

std::unique_ptr<Module> make() { return std::make_unique<Sampler>(); }

}  // namespace

const ModuleInfo sampler_info{"sampler", kIns, kCfg, kOuts, &make};

}  // namespace ms::dsp
