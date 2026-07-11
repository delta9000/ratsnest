#include <doctest/doctest.h>
#include <cstdint>
#include <thread>
#include "rt/denormal.h"
#include "rt/spsc.h"

using ms::rt::SpscQueue;

TEST_CASE("spsc: fifo order, empty and full signalling") {
  SpscQueue<int, 4> q;  // usable capacity 3
  int v = -1;
  CHECK(!q.try_pop(v));
  CHECK(q.try_push(1));
  CHECK(q.try_push(2));
  CHECK(q.try_push(3));
  CHECK(!q.try_push(4));  // full at Cap-1
  REQUIRE(q.try_pop(v)); CHECK(v == 1);
  REQUIRE(q.try_pop(v)); CHECK(v == 2);
  CHECK(q.try_push(4));
  REQUIRE(q.try_pop(v)); CHECK(v == 3);
  REQUIRE(q.try_pop(v)); CHECK(v == 4);
  CHECK(!q.try_pop(v));
}

TEST_CASE("spsc: wraps many times without loss") {
  SpscQueue<uint32_t, 8> q;
  uint32_t out = 0, next = 0;
  for (uint32_t i = 0; i < 1000; ++i) {
    REQUIRE(q.try_push(i));
    if (i % 3 == 2) {  // drain in bursts so indices wrap repeatedly
      while (q.try_pop(out)) { REQUIRE(out == next); ++next; }
    }
  }
  while (q.try_pop(out)) { REQUIRE(out == next); ++next; }
  CHECK(next == 1000);
}

TEST_CASE("spsc: threaded stress preserves order and loses nothing") {
  SpscQueue<uint32_t, 1024> q;
  constexpr uint32_t kN = 200000;
  std::thread producer([&] {
    for (uint32_t i = 0; i < kN;)
      if (q.try_push(i)) ++i;
  });
  uint32_t expect = 0, got = 0;
  while (expect < kN)
    if (q.try_pop(got)) {
      if (got != expect) { producer.join(); FAIL("out of order at ", expect); }
      ++expect;
    }
  producer.join();
  CHECK(expect == kN);
}

TEST_CASE("denormals flush to zero after enabling") {
#if defined(__SSE2__)
  ms::rt::flush_denormals_on_this_thread();
  volatile float v = 1e-38f;       // smallest-ish normal float
  volatile float w = v * 0.001f;   // would be a nonzero subnormal without FTZ
  CHECK(w == 0.0f);                // FTZ flushes it to exactly zero
#endif
}
