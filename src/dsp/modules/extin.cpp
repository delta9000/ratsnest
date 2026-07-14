#include <array>
#include <cstddef>
#include "dsp/modules.h"

namespace ms::dsp {
namespace {

constexpr OutputInfo kOuts[] = {{"out"}};

// External audio capture source. The engine delivers captured samples via
// on_audio_in at block start; process() doles them out one per sample. Both run
// on the audio thread, so the internal ring needs no atomics. Underrun (capture
// slower than playback, or none at all offline) emits silence; overrun (capture
// faster) drops the oldest samples. This slack absorbs capture/playback clock
// drift.
class Extin final : public Module {
 public:
  void on_audio_in(const float* samples, size_t n) override {
    for (size_t i = 0; i < n; ++i) {
      ring_[head_] = samples[i];
      head_ = (head_ + 1) & kMask;
      if (count_ < kCap) {
        ++count_;
      } else {
        tail_ = (tail_ + 1) & kMask;  // full: drop the oldest sample
      }
    }
  }

  void process(const float* /*ins*/, float* outs) override {
    float v = 0.0f;
    if (count_ > 0) {
      v = ring_[tail_];
      tail_ = (tail_ + 1) & kMask;
      --count_;
    }
    outs[0] = v;
  }

  void copy_state_from(const Module& other) override {
    const auto& o = static_cast<const Extin&>(other);
    ring_ = o.ring_;
    head_ = o.head_;
    tail_ = o.tail_;
    count_ = o.count_;
  }

 private:
  static constexpr size_t kCap = 4096;  // power of two
  static constexpr size_t kMask = kCap - 1;
  std::array<float, kCap> ring_{};
  size_t head_ = 0, tail_ = 0, count_ = 0;
};

std::unique_ptr<Module> make() { return std::make_unique<Extin>(); }

}  // namespace

const ModuleInfo extin_info{"extin", {}, {}, kOuts, &make,
                            /*wants_midi=*/false, /*wants_audio_in=*/true};

}  // namespace ms::dsp
