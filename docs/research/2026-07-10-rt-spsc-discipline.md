# Engineering Brief: RT-Safe SPSC Queues & Graph-Swap for a Modular Synth Engine

## 1. RT-thread discipline (Bencina's rules)

The cardinal rule (Bencina, *Real-time audio programming 101*): **"If you don't know how long it will take, don't do it."** If the callback overruns the buffer period, audio glitches. Forbidden inside `Module::process()` and anything reachable from the audio thread:

- **Locks/mutexes** — not because locking is slow, but **priority inversion**: your RT thread runs at `SCHED_FIFO`; if it blocks on a mutex held by a normal-priority non-RT thread, and that thread is preempted by any other ready thread, the RT thread is stalled indefinitely by threads it outranks. Unbounded, unpredictable → glitch. (Bencina; farbot README.)
- **`malloc`/`free`/`new`/`delete`** — the allocator takes an internal lock and has unbounded worst-case time. This bans hidden allocations too: `std::function` assignment (may heap-allocate), `std::vector`/`std::string` growth, `shared_ptr` **copies** (atomic refcount inc is fine timing-wise but the eventual last-ref `delete` runs the destructor + `free` — never let a `shared_ptr` reach zero on the RT thread).
- **I/O and syscalls** — `printf`/logging, file access, GUI calls, anything that may block. (Bencina)
- **Unbounded/data-dependent loops** — prefer O(1) worst-case; amortize work across samples rather than "bursty" spikes. (Bencina)

Rule of thumb: also don't call any code you don't *trust* to obey the above (transitive).

## 2. Minimal C++20 SPSC command ring (trivially-copyable structs)

For commands **into** the RT thread (glide targets, new-graph pointer) and pointers **out** (old graph to free).

```cpp
template<class T, size_t Cap>  // Cap MUST be power of 2
class SpscQueue {
  static_assert(std::is_trivially_copyable_v<T>);
  static_assert((Cap & (Cap-1)) == 0);
  alignas(64) std::atomic<size_t> head_{0};   // consumer writes, producer reads
  alignas(64) std::atomic<size_t> tail_{0};   // producer writes, consumer reads
  alignas(64) size_t cachedHead_{0};          // producer-local
  size_t cachedTail_{0};                       // consumer-local
  alignas(64) std::array<T, Cap> buf_;
public:
  bool try_push(const T& v) {                  // producer thread only
    const size_t t = tail_.load(std::memory_order_relaxed);
    const size_t next = (t + 1) & (Cap - 1);
    if (next == cachedHead_) {                 // check cache first
      cachedHead_ = head_.load(std::memory_order_acquire);   // refresh
      if (next == cachedHead_) return false;   // truly full
    }
    buf_[t] = v;                               // write payload BEFORE publishing
    tail_.store(next, std::memory_order_release);
    return true;
  }
  bool try_pop(T& out) {                        // consumer thread only
    const size_t h = head_.load(std::memory_order_relaxed);
    if (h == cachedTail_) {
      cachedTail_ = tail_.load(std::memory_order_acquire);   // refresh
      if (h == cachedTail_) return false;      // truly empty
    }
    out = buf_[h];                              // read payload AFTER acquire
    head_.store((h + 1) & (Cap - 1), std::memory_order_release);
    return true;
  }
};
```

**Memory ordering (spell it out):** the producer's payload write (`buf_[t] = v`) must be visible before the consumer sees the advanced index. So producer publishes with `tail_.store(release)`; consumer reads with `tail_.load(acquire)`. Symmetrically the consumer frees a slot with `head_.store(release)` and the producer observes it with `head_.load(acquire)`. Each thread reads *its own* index with `relaxed` (no other thread writes it). This is the exact pattern PipeWire's `spa_ringbuffer` uses: `get_read_index` loads its own index relaxed, the peer's `__ATOMIC_ACQUIRE`; `read_update`/`write_update` store `__ATOMIC_RELEASE` (PipeWire `ringbuffer.h`). moodycamel confirms the acquire/release pairing and notes x86 gives it implicitly (every load is acquire, every store release) but the atomics are still needed to stop **compiler** reordering and for ARM.

**Cached indices** (moodycamel optimization): the producer keeps `cachedHead_`, only re-loading the real `head_` when the cache says "full." This avoids touching the consumer's cache line on the common path — cutting the cross-core traffic that dominates SPSC cost.

**False sharing:** `head_`, `tail_`, and each cached index sit on separate 64-byte lines (`alignas(64)`). Without this, producer and consumer thrash the same line and throughput collapses.

**Capacity−1 usable slots:** classic "one wasted slot" scheme (`next == head` ⇒ full) — moodycamel does the same because "if front == tail the queue is empty and can't be full," making empty/full unambiguous without a separate count. The alternative (free-running counters, mask only for indexing, `size = tail - head` gives full capacity — what PipeWire/JACK do) is equally valid; masked indices + one wasted slot is simpler and one slot is irrelevant at our sizes. **Do not** free-run *masked* indices and also try to use full capacity — that reintroduces the ambiguity.

## 3. Graph-swap pattern

Non-RT (compiler) builds the new `Graph*` fully, then:

1. Non-RT `try_push`es the new `Graph*` on the **inbound** command queue.
2. RT thread, **at block start only**, pops it, swaps its local `Graph* current_`, and `try_push`es the **old** pointer onto a **return** queue.
3. Non-RT thread periodically drains the return queue and `delete`s the old graphs (runs all destructors/`free` off the RT thread).

**Why never delete on RT:** `delete` = destructor chain + `free` = unbounded, locking. The RT thread only ever does a pointer store and a queue push — both O(1), lock-free.

**Return-queue sizing / backpressure:** size it so it *cannot* fill — capacity ≥ max in-flight graphs. The clean invariant: the non-RT thread **must not push a new swap until it has drained and freed the previous old graph** (gate new swaps on the return queue being empty / on receiving the prior graph back). Then at most one graph is ever in flight each direction and a capacity of 2–4 is provably enough; `try_push` on the return queue can never fail, so the RT thread never has to make a decision about "what if full." If you can't guarantee that, on a full return queue the RT thread must keep running the old graph and drop the swap (never block, never delete) — but the "gate until drained" design avoids that entirely.

**Simpler `std::atomic<Graph*>` exchange alternative — viable but has pitfalls.** RT does `old = live.exchange(new, acquire)`; but *who frees `old`?* If the RT thread returns it via a queue anyway, you've just re-added the queue. If the non-RT thread frees it, it needs a handshake to know the RT thread has stopped touching it — a bare `exchange` gives no such signal, so you get a use-after-free race (RT still mid-block on the graph the non-RT thread just deleted). Also an ABA-flavored hazard if graph memory is recycled from a pool. **Recommendation:** use the two-queue (in + return) design; it makes ownership handoff explicit. This is exactly farbot's `RealtimeObject<T, nonRealtimeMutatable>` model.

## 4. Ring buffer for float data OUT (meters/scope)

Different from the command queue in two ways:

- **Bulk contiguous writes**, not one struct at a time — use a byte/float ring with two-part (wrapped) read/write vectors, like JACK's `jack_ringbuffer_t` (`get_write_vector`/`get_read_vector` give zero-copy pointer+len pairs across the wrap) or CHOC's `SingleReaderSingleWriterFIFO`. Same acquire/release discipline as §2.
- **Policy on contention:** for **commands** you *drop-newest* (`try_push` fails, don't clobber). For **meters/scope** you *overwrite-oldest* — the RT producer keeps writing and never stalls; if the non-RT reader falls behind it just misses old samples, fine for a display. Meters can go further: a single `std::atomic<float>` peak per channel, RT does `store(release)`, UI `load(acquire)` — no ring needed for a level meter. Use the float ring only for scope/record where you need a contiguous window.

## 5. Denormals (x86)

Denormal floats (feedback tails, filter/reverb decays) cost 10–100× on x86 and cause CPU spikes that read as glitches. Set **FTZ (flush-to-zero)** + **DAZ (denormals-are-zero)**:

```cpp
#include <pmmintrin.h>
_MM_SET_FLUSH_ZERO_MODE(_MM_FLUSH_ZERO_ON);       // FTZ
_MM_SET_DENORMAL_ZERO_MODE(_MM_DENORMALS_ZERO_ON); // DAZ  (needs SSE3)
```

These flags are **per-thread (per-core MXCSR)**, not global. PipeWire runs `process` on *its* data thread and does **not** guarantee FTZ/DAZ set. So set it **at the top of every `process` callback** (cheap: one `stmxcsr`/`ldmxcsr`), not just once at thread start — you don't own thread creation, and a thread migration or PipeWire reset could clear it. Additionally keep **per-module NaN/Inf guards** at feedback boundaries (`if (!std::isfinite(x)) x = 0.f;`) — FTZ handles denormals but not NaN poisoning from `0/0` or `log(neg)`, which propagates through the whole graph once introduced.

## 6. Bugs & gotchas checklist

- **False sharing** — head/tail/caches unaligned share a line → throughput cliff. Fix: `alignas(64)`.
- **`relaxed` where `acquire` needed** — reader sees the advanced index but a *stale/torn* payload. The index publish must be `release` and the peer's read `acquire`; getting this wrong is the #1 silent SPSC bug (moodycamel).
- **`%` with non-power-of-2 capacity** — slow division and wrong wrap if you later switch to free-running counters. Keep `Cap` a power of 2 and mask with `& (Cap-1)`.
- **Free-running vs masked indices** — free-running `size_t` counters that increment forever are fine (wrap is well-defined for unsigned; `tail - head` gives true occupancy — JACK/PipeWire style) *but then mask only when indexing*. Don't mix: masked-stored indices + occupancy-by-subtraction breaks.
- **ABA** — a genuine non-issue for *SPSC*: exactly one producer and one consumer, indices are monotonic, no CAS loop. (It only bites MPMC/pointer-reuse pools — see §3 exchange caveat.)
- **Torn reads of non-atomic payload** — never read `buf_[h]` before the `acquire` load that proves it's committed; never overwrite `buf_[t]` after the `release` that published it.
- **Compiler reordering** — even on x86 (where hardware gives acquire/release free), the atomics are still mandatory to fence the *compiler* (moodycamel).
- **`seq_cst` everywhere** — correct but adds `mfence`/`lock` on stores; use it only as a temporary "make it correct first" fallback, then tighten to acquire/release.

## 7. PipeWire specifics

- The `process` callback runs on the mainloop **unless** you set `PW_FILTER_FLAG_RT_PROCESS`, which makes it run on the **realtime data thread** — you want this, and then *only RT-safe PipeWire methods* may be called from it (PipeWire `pw_filter_events` docs). All §1 rules apply there.
- RT scheduling comes from **`module-rt`**: it grants `SCHED_FIFO`-style priority to the data thread via `RLIMIT_RTPRIO`, or falls back to RealtimeKit/RTKit D-Bus. Configurable via `rt.prio`, plus `rt.time.soft`/`rt.time.hard` CPU-time watchdog limits. Ensure the user has `RLIMIT_RTPRIO` (via `/etc/security/limits.d` or the `realtime` group) or RTKit is running.
- **mlock:** PipeWire attempts to `mlock` its own processing memory. For **your** engine's graph/buffer memory the safest move is `mlockall(MCL_CURRENT|MCL_FUTURE)` at startup (JACK exposes `jack_ringbuffer_mlock` for exactly this reason). Do this once at init on the non-RT side.

## Source map
- **Forbidden ops, cardinal rule, priority inversion** — Ross Bencina, *Real-time audio programming 101* (rossbencina.com; LWN 452630).
- **Acquire/release pairing, cached indices, wasted-slot full/empty, x86-implicit ordering, compiler-fence caveat, ABA-monotonic** — moodycamel `ReaderWriterQueue`.
- **Relaxed-own / acquire-peer / release-update index protocol, free-running masked counters** — PipeWire `spa/utils/ringbuffer.h`.
- **Graph pointer-swap + deferred non-RT deletion, `RealtimeObject<nonRealtimeMutatable>`** — farbot (hogliux/farbot README).
- **Bulk two-part read/write vectors + `mlock`** — JACK `jack_ringbuffer`; API shape also CHOC `SingleReaderSingleWriterFIFO`.
- **RT process thread, `PW_FILTER_FLAG_RT_PROCESS`, `module-rt` priority, mlock** — PipeWire docs.

(Caveat from researcher: Bencina original and JACK/CHOC headers were summarized from secondary sources, not fresh line-by-line reads.)
