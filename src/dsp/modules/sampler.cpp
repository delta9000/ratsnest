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
        file_rate_ = static_cast<float>(w.sample_rate);
      }
    }
    pos_ = 0.0;
    playing_ = !buf_.empty();  // play once at load; trig can restart
    trig_edge_ = RisingEdge{};
  }

  void process(const float* ins, float* outs) override {
    const float trig = ins[0];
    // Clamped like svf/ladder v_oct: a non-finite or absurd pitch would make
    // pos_ non-finite, and casting that to an index is undefined.
    const float pitch = sanitize_clamp(ins[1], -24.0f, 24.0f);
    const float level = ins[2];

    // Rising edge on trig restarts playback from the beginning.
    if (trig_edge_(trig)) {
      pos_ = 0.0;
      playing_ = !buf_.empty();
    }

    float out = 0.0f;
    float eos = 0.0f;

    if (playing_ && !buf_.empty()) {
      const size_t len = buf_.size();
      const size_t i0 = static_cast<size_t>(pos_);
      const float frac = static_cast<float>(pos_ - static_cast<double>(i0));
      const float a = buf_[i0];
      const float b = (i0 + 1 < len) ? buf_[i0 + 1] : (loop_ ? buf_[0] : buf_[i0]);
      out = (a + (b - a) * frac) * level;

      const double step = static_cast<double>(file_rate_ / engine_rate_) *
                          std::pow(2.0, static_cast<double>(pitch));
      pos_ += step;
      if (pos_ >= static_cast<double>(len)) {
        if (loop_) {
          pos_ = std::fmod(pos_, static_cast<double>(len));
        } else {
          playing_ = false;
          eos = 1.0f;
        }
      }
    }

    outs[0] = out;
    outs[1] = eos;
  }

  void copy_state_from(const Module& other) override {
    const auto& o = static_cast<const Sampler&>(other);
    pos_ = o.pos_;
    playing_ = o.playing_;
    trig_edge_ = o.trig_edge_;
    // A reload may have loaded a shorter buffer; keep the read position valid.
    if (pos_ >= static_cast<double>(buf_.size())) {
      pos_ = 0.0;
      playing_ = false;
    }
  }

 private:
  float engine_rate_ = 48000.0f;
  float file_rate_ = 48000.0f;
  bool loop_ = false;
  std::vector<float> buf_;
  double pos_ = 0.0;
  bool playing_ = false;
  RisingEdge trig_edge_;
};

std::unique_ptr<Module> make() { return std::make_unique<Sampler>(); }

}  // namespace

const ModuleInfo sampler_info{"sampler", kIns, kCfg, kOuts, &make};

}  // namespace ms::dsp
