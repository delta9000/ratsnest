#include <algorithm>
#include <cmath>
#include "dsp/modules.h"
#include "dsp/mastering.h"

namespace ms::dsp {
namespace {

constexpr InputInfo kIns[] = {
    {"in_l", 0.0f},
    {"in_r", 0.0f},
    {"threshold", -0.1f}, // dBFS
    {"release", 0.05f}    // s
};
constexpr OutputInfo kOuts[] = {
    {"out_l"},
    {"out_r"},
    {"gain"},
    {"gr"}
};

// Fixed-capacity circular deque used for the O(1) sliding-window maximum.
// Stores ring-buffer indices in monotonically decreasing order of maxbuf_ value.
struct SlideMaxDeque {
  int buf[512] = {};
  int head_ = 0;
  int tail_ = 0; // one past the last element

  void clear() { head_ = 0; tail_ = 0; }
  bool empty() const { return head_ == tail_; }
  void push_back(int v) { buf[tail_] = v; tail_ = (tail_ + 1) % 512; }
  void pop_back()  { tail_ = (tail_ + 511) % 512; }
  void pop_front() { head_ = (head_ + 1)   % 512; }
  int  front() const { return buf[head_]; }
  int  back()  const { return buf[(tail_ + 511) % 512]; }
};

class Limiter final : public Module {
 public:
  void init(double rate, const Config& cfg) override {
    (void)cfg;
    rate_ = static_cast<float>(rate);
    
    g_smooth_ = 0.0f;
    
    // Clear ring buffers and sliding-max state
    std::fill(std::begin(buffer_l_), std::end(buffer_l_), 0.0f);
    std::fill(std::begin(buffer_r_), std::end(buffer_r_), 0.0f);
    std::fill(std::begin(maxbuf_),   std::end(maxbuf_),   0.0f);
    write_idx_ = 0;
    deq_.clear();
  }

  void process(const float* ins, float* outs) override {
    // 1. Sanitize audio inputs and write to ring buffers
    const float in_l = sanitize(ins[0], 0.0f);
    const float in_r = sanitize(ins[1], 0.0f);
    
    buffer_l_[write_idx_] = in_l;
    buffer_r_[write_idx_] = in_r;
    
    // Sanitize and clamp control inputs
    const float threshold = sanitize_clamp(ins[2], -24.0f, 0.0f, -0.1f);
    const float release = sanitize_clamp(ins[3], 0.001f, 2.0f, 0.05f);
    
    // 2. Calculate delay size (2.0 ms lookahead).
    //    Cap at 511 so the leaving index can never alias the write index.
    const int delay_samples = std::clamp(
        static_cast<int>(std::round(rate_ * 0.002f)),
        1,
        511
    );

    // 3. Sliding-window peak via monotonic deque — O(1) amortised.
    //    maxbuf_ caches max(|L|, |R|) at each ring-buffer position.
    maxbuf_[write_idx_] = std::max({std::abs(in_l), std::abs(in_r), 1e-12f});

    // Remove the position that just fell outside the window as write_idx_ advanced.
    // When we move from T-1 to T the stale position is T-delay_samples-1.
    const int leaving = (write_idx_ - delay_samples - 1 + 1024) % 512;
    if (!deq_.empty() && deq_.front() == leaving) {
      deq_.pop_front();
    }
    // Maintain monotonically-decreasing order: drop smaller tail entries.
    while (!deq_.empty() && maxbuf_[deq_.back()] <= maxbuf_[write_idx_]) {
      deq_.pop_back();
    }
    deq_.push_back(write_idx_);
    const float max_peak = maxbuf_[deq_.front()];
    
    // 4. Calculate gain reduction target in dB
    const float peak_db = 20.0f * std::log10(max_peak);
    float g_target = peak_db - threshold;
    if (g_target < 0.0f) g_target = 0.0f;
    
    // 5. Smooth gain reduction with fast attack (0.1 ms) and variable release
    const float alpha_a = 1.0f - std::exp(-1.0f / (0.0001f * rate_));
    const float alpha_r = 1.0f - std::exp(-1.0f / (release * rate_));
    
    if (g_target > g_smooth_) {
      g_smooth_ += alpha_a * (g_target - g_smooth_);
    } else {
      g_smooth_ += alpha_r * (g_target - g_smooth_);
    }
    g_smooth_ = flush_denormal(g_smooth_);
    
    // 6. Convert gain reduction to linear factor
    const float g_linear = std::pow(10.0f, -g_smooth_ / 20.0f);
    
    // 7. Process delayed audio from the ring buffer
    const int read_idx = (write_idx_ - delay_samples + 512) % 512;
    const float dry_l = buffer_l_[read_idx];
    const float dry_r = buffer_r_[read_idx];
    
    outs[0] = dry_l * g_linear;
    outs[1] = dry_r * g_linear;
    outs[2] = g_linear;
    outs[3] = g_smooth_;
    
    // Increment write index
    write_idx_ = (write_idx_ + 1) % 512;
  }

  void copy_state_from(const Module& other) override {
    const auto& o = static_cast<const Limiter&>(other);
    g_smooth_ = o.g_smooth_;
    std::copy(std::begin(o.buffer_l_), std::end(o.buffer_l_), std::begin(buffer_l_));
    std::copy(std::begin(o.buffer_r_), std::end(o.buffer_r_), std::begin(buffer_r_));
    std::copy(std::begin(o.maxbuf_),   std::end(o.maxbuf_),   std::begin(maxbuf_));
    write_idx_ = o.write_idx_;
    deq_ = o.deq_;
  }

 private:
  float rate_ = 48000.0f;
  float g_smooth_ = 0.0f;
  
  // Ring buffers
  float buffer_l_[512] = {};
  float buffer_r_[512] = {};
  float maxbuf_[512]   = {};  // max(|L|,|R|) at each position for sliding-window max
  int write_idx_ = 0;
  SlideMaxDeque deq_;          // monotonic deque for O(1) lookahead peak extraction
};

std::unique_ptr<Module> make() { return std::make_unique<Limiter>(); }

}  // namespace

const ModuleInfo limiter_info{"limiter", kIns, {}, kOuts, &make};

}  // namespace ms::dsp
