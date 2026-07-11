#pragma once
#include <array>
#include <atomic>
#include <cstddef>
#include <type_traits>

namespace ms::rt {

// Lock-free single-producer single-consumer ring. Exactly one thread may call
// try_push and exactly one thread may call try_pop. Wait-free, no allocation.
// Usable capacity is Cap-1: `next == head` disambiguates full from empty.
// Memory ordering: the payload write must be visible before the index that
// publishes it, so each index is stored with release and read by the peer
// with acquire; each side reads its own index relaxed (nobody else writes it).
// Cached peer indices avoid touching the other core's cache line on the
// common path (moodycamel optimization); alignas(64) prevents false sharing.
// See docs/research/2026-07-10-rt-spsc-discipline.md.
template <typename T, size_t Cap>
class SpscQueue {
  static_assert(std::is_trivially_copyable_v<T>);
  static_assert(Cap >= 2 && (Cap & (Cap - 1)) == 0, "Cap must be a power of 2");

 public:
  bool try_push(const T& v) {
    const size_t t = tail_.load(std::memory_order_relaxed);
    const size_t next = (t + 1) & kMask;
    if (next == cached_head_) {
      cached_head_ = head_.load(std::memory_order_acquire);
      if (next == cached_head_) return false;  // truly full
    }
    buf_[t] = v;
    tail_.store(next, std::memory_order_release);
    return true;
  }

  bool try_pop(T& out) {
    const size_t h = head_.load(std::memory_order_relaxed);
    if (h == cached_tail_) {
      cached_tail_ = tail_.load(std::memory_order_acquire);
      if (h == cached_tail_) return false;  // truly empty
    }
    out = buf_[h];
    head_.store((h + 1) & kMask, std::memory_order_release);
    return true;
  }

 private:
  static constexpr size_t kMask = Cap - 1;
  alignas(64) std::atomic<size_t> head_{0};  // consumer advances, producer reads
  alignas(64) std::atomic<size_t> tail_{0};  // producer advances, consumer reads
  alignas(64) size_t cached_head_ = 0;       // producer-local
  alignas(64) size_t cached_tail_ = 0;       // consumer-local
  alignas(64) std::array<T, Cap> buf_{};
};

}  // namespace ms::rt
