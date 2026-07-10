#pragma once
#include <cstdint>
#include <cstddef>
#include <memory>
#include <string>

namespace ms::engine {

// Stereo float32 PipeWire source via pw_filter. The render callback runs on
// PipeWire's RT data thread: it must be RT-safe (this class guarantees its
// own process path is; the callback's body is the caller's contract).
// No PipeWire types leak from this header (pimpl) so the -Wconversion
// diagnostic wrapper stays confined to the .cpp.
class PipewireOut {
 public:
  using RenderFn = void (*)(void* user, float* l, float* r, size_t frames);

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
