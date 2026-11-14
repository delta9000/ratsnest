#pragma once
#include <cstdint>
#include <cstddef>
#include <memory>
#include <string>

namespace ms::engine {

// Physical device boundary only; graph processing and float WAVs keep headroom.
float device_sample(float sample);

// Stereo float32 PipeWire source via pw_stream. The render callback runs on
// PipeWire's RT data thread: it must be RT-safe (this class guarantees its
// own process path is; the callback's body is the caller's contract).
// No PipeWire types leak from this header (pimpl) so the -Wconversion
// diagnostic wrapper stays confined to the .cpp.
class PipewireOut {
 public:
  // now_ns: the cycle's CLOCK_MONOTONIC start (spa_io_position clock.nsec),
  // 0 when PipeWire has not given the stream a position.
  using RenderFn = void (*)(void* user, float* l, float* r, size_t frames, uint64_t now_ns);

  PipewireOut(const char* app_name, RenderFn fn, void* user,
              const char* latency, uint32_t requested_rate);
  ~PipewireOut();
  PipewireOut(const PipewireOut&) = delete;
  PipewireOut& operator=(const PipewireOut&) = delete;

  bool ok() const;
  std::string error() const;
  uint32_t observed_rate() const;
  uint64_t xruns() const;

 private:
  struct Impl;
  std::unique_ptr<Impl> impl_;
};

}  // namespace ms::engine
