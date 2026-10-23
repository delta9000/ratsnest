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
    buf_.clear();
    len_ = 0;
    file_rate_ = engine_rate_;

    const std::string file = cfg_str(cfg, "file", "");
    if (!file.empty()) {
      io::WavData w;
      if (io::read_wav_f32(file, w) && w.frames > 0 && w.channels > 0) {
        // Mix any channel count down to a single mono buffer.
        buf_.resize(w.frames);
        const size_t ch = static_cast<size_t>(w.channels);
        for (size_t fr = 0; fr < w.frames; ++fr) {
          float acc = 0.0f;
          for (size_t c = 0; c < ch; ++c) acc += w.samples[fr * ch + c];
          buf_[fr] = acc / static_cast<float>(w.channels);
        }
        len_ = buf_.size();
        // Guard frame: interpolation past the last frame wraps to the first
        // when looping and holds the last otherwise.
        buf_.push_back(loop_ ? buf_.front() : buf_.back());
        file_rate_ = static_cast<float>(w.sample_rate);
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
    if (playing_) {
      out = read(pos_);
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
      out += read(old_pos_) * (old_left_ / fade_);
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
  // Linear interpolation at pos in [0, len_), reading the guard frame last.
  float read(double pos) const {
    const size_t i0 = static_cast<size_t>(pos);
    const float frac = static_cast<float>(pos - static_cast<double>(i0));
    return buf_[i0] + (buf_[i0 + 1] - buf_[i0]) * frac;
  }

  float engine_rate_ = 48000.0f;
  float file_rate_ = 48000.0f;
  bool loop_ = false;
  std::vector<float> buf_;  // len_ frames plus one guard frame
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
