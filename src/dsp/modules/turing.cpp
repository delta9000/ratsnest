#include <algorithm>
#include <cmath>
#include <cstdint>
#include "dsp/modules.h"

// Concept-inspired by the Music Thing Modular Turing Machine; no firmware
// code was copied. See docs/attribution.md.

namespace ms::dsp {
namespace {
constexpr InputInfo kIns[] = {{"clock", 0.f}, {"write", 0.f}, {"clear", 0.f},
                              {"len", 8.f}, {"prob", 0.1f}};
constexpr ConfigInfo kCfg[] = {{"seed", nullptr}};
constexpr OutputInfo kOuts[] = {{"out"}, {"pulse"}, {"bit1"}, {"bit2"},
                                {"bit3"}, {"bit4"}, {"bit5"}, {"bit6"},
                                {"bit7"}, {"bit8"}};
int length(float value) {
  if (!std::isfinite(value)) return 8;
  return static_cast<int>(std::lround(std::clamp(value, 1.0f, 16.0f)));
}
class Turing final : public Module {
 public:
  void init(double, const Config& cfg) override {
    double seed = cfg_num(cfg, "seed", 1.0);
    if (!std::isfinite(seed)) seed = 1.0;
    rng_ = static_cast<std::uint32_t>(
               std::clamp(std::abs(seed), 0.0, 4294967295.0)) * 2654435761u;
    if (rng_ == 0) rng_ = 0x9E3779B9u;
    reg_ = static_cast<std::uint16_t>(rng_ ^ (rng_ >> 16));
  }
  void process(const float* ins, float* outs) override {
    const bool rising = ins[0] > 0.5f && prev_clock_ <= 0.5f;
    prev_clock_ = ins[0];
    bool pulse = false;
    if (rising) {
      const int len = length(ins[3]);
      const std::uint32_t mask = (1u << len) - 1u;
      if (ins[2] > 0.5f) {
        reg_ = 0;
      } else {
        bool bit = ((reg_ >> (len - 1)) & 1u) != 0;
        const float p = std::clamp(ins[4], 0.0f, 1.0f);
        if (p >= 1.0f || (p > 0.0f && draw() < p)) bit = !bit;
        if (ins[1] > 0.5f) bit = true;
        reg_ = static_cast<std::uint16_t>(((static_cast<std::uint32_t>(reg_) << 1) |
                                           static_cast<std::uint32_t>(bit)) & mask);
        pulse = bit;
      }
    }
    outs[0] = static_cast<float>(reg_ & 0xffu) * (1.0f / 255.0f);
    outs[1] = pulse ? 1.0f : 0.0f;
    for (int i = 0; i < 8; ++i) outs[2 + i] = (reg_ >> i) & 1u ? 1.0f : 0.0f;
  }
  void copy_state_from(const Module& other) override {
    const auto& o = static_cast<const Turing&>(other);
    rng_ = o.rng_; reg_ = o.reg_; prev_clock_ = o.prev_clock_;
 }
 private:
  // Marsaglia xorshift32; see docs/attribution.md.
  float draw() {
    std::uint32_t x = rng_;
    x ^= x << 13; x ^= x >> 17; x ^= x << 5;
    rng_ = x;
    return static_cast<float>(x) * (1.0f / 4294967296.0f);
  }
  std::uint32_t rng_ = 0x9E3779B9u;
  std::uint16_t reg_ = 1;
  float prev_clock_ = 0.0f;
};
std::unique_ptr<Module> make() { return std::make_unique<Turing>(); }
}  // namespace
const ModuleInfo turing_info{"turing", kIns, kCfg, kOuts, &make};
}  // namespace ms::dsp
