#include <string>
#include <vector>
#include "dsp/modules.h"
#include "io/wav.h"

namespace ms::dsp {
namespace {

constexpr InputInfo kIns[] = {
    {"in_l", 0.0f},
    {"in_r", 0.0f},
    {"gate", 1.0f},
};
constexpr ConfigInfo kCfg[] = {
    {"file", nullptr, ConfigKind::Text},   // arbitrary path string (quoted in the patch)
    {"secs", nullptr},   // capture cap in seconds (numeric)
};

// Records its stereo input to a WAV file. Capture happens in process() into a
// preallocated RAM buffer (RT-safe: no alloc, no I/O); the file is written once
// at teardown, on the non-RT thread that retires the engine. A sink with no
// outputs, like scope.
class Rec final : public Module {
 public:
  void init(double rate, const Config& cfg) override {
    rate_ = static_cast<int>(rate);
    file_ = cfg_str(cfg, "file", "");
    double secs = cfg_num(cfg, "secs", 30.0);
    if (secs < 1.0) secs = 1.0;
    if (secs > 600.0) secs = 600.0;
    capacity_ = static_cast<size_t>(secs * rate);
    buf_.assign(capacity_ * 2, 0.0f);  // interleaved stereo
    count_ = 0;
  }

  void process(const float* ins, float* /*outs*/) override {
    if (ins[2] > 0.5f && count_ < capacity_) {  // gate high, room left
      buf_[count_ * 2] = ins[0];
      buf_[count_ * 2 + 1] = ins[1];
      ++count_;
    }
  }

  // No copy_state_from override: the capture buffer is deliberately not
  // migrated across hot reload (a large memcpy on the RT thread would xrun).
  // A reload restarts the take; the outgoing instance flushes here.
  bool finish() override {
    if (file_.empty() || count_ == 0) return true;
    const size_t frames = count_;
    count_ = 0;
    if (!io::write_wav_take(file_, buf_.data(), frames, rate_)) {
      std::fprintf(stderr, "error: cannot write recording '%s'\n", file_.c_str());
      return false;
    }
    std::fprintf(stderr, "recorded %zu frames -> %s\n", frames, file_.c_str());
    return true;
  }

  ~Rec() override { finish(); }

 private:
  int rate_ = 48000;
  std::string file_;
  std::vector<float> buf_;
  size_t capacity_ = 0;
  size_t count_ = 0;
};

std::unique_ptr<Module> make() { return std::make_unique<Rec>(); }

}  // namespace

const ModuleInfo rec_info{"rec", kIns, kCfg, {}, &make};

}  // namespace ms::dsp
