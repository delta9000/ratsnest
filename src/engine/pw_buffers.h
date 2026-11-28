#pragma once
// The buffer work inside the PipeWire callbacks (pipewire_out, audio_in,
// midi_in), apart from the dequeue/queue calls, so tests can drive it with
// hand-built spa buffers and pods: no daemon needed. All of it runs on a
// PipeWire RT thread, so none of it allocates, locks or does I/O.
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wconversion"
#pragma GCC diagnostic ignored "-Wsign-conversion"
#include <spa/buffer/buffer.h>
#include <spa/control/control.h>
#include <spa/control/ump-utils.h>
#include <spa/node/io.h>
#include <spa/pod/iter.h>
#include <spa/pod/pod.h>
#pragma GCC diagnostic pop

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <cstring>

#include "engine/rt_runner.h"
#include "io/midi_bytes.h"

namespace ms::engine::pw {

// --- Playback: two planar float planes --------------------------------------

// Frames to render into a playback buffer: what PipeWire asked for (all of
// it when it asked for 0), bounded by the smaller plane. 0 when either plane
// is missing.
inline uint32_t playback_frames(const spa_buffer& buf, uint64_t requested) {
  if (buf.n_datas < 2 || buf.datas[0].data == nullptr || buf.datas[1].data == nullptr) return 0;
  const uint32_t cap = std::min(buf.datas[0].maxsize, buf.datas[1].maxsize) /
                       static_cast<uint32_t>(sizeof(float));
  const uint64_t want = requested != 0 ? requested : cap;
  return static_cast<uint32_t>(std::min<uint64_t>(want, cap));
}

// Marks both planes as holding `frames` floats from offset 0.
inline void mark_planes(spa_buffer& buf, uint32_t frames) {
  for (uint32_t c = 0; c < 2; ++c) {
    buf.datas[c].chunk->offset = 0;
    buf.datas[c].chunk->stride = static_cast<int32_t>(sizeof(float));
    buf.datas[c].chunk->size = frames * static_cast<uint32_t>(sizeof(float));
  }
}

// --- Capture: one mono float plane ------------------------------------------

// Hands the captured samples to `emit` in AudioChunks of at most 64. The
// chunk's offset and size are clamped to the plane, so a bad header reads
// short rather than past the buffer; a stride wider than a float (a peer
// that sent interleaved frames) reads the first channel of each frame.
template <class Emit>
void capture_chunks(const spa_buffer& buf, Emit&& emit) {
  if (buf.n_datas < 1 || buf.datas[0].data == nullptr || buf.datas[0].chunk == nullptr) return;
  const spa_data& d = buf.datas[0];
  const uint32_t offset = std::min(d.chunk->offset, d.maxsize);
  const uint32_t size = std::min(d.chunk->size, d.maxsize - offset);
  const uint32_t stride = d.chunk->stride >= static_cast<int32_t>(sizeof(float))
      ? static_cast<uint32_t>(d.chunk->stride)
      : static_cast<uint32_t>(sizeof(float));
  const uint32_t n = size / stride;
  const auto* base = static_cast<const uint8_t*>(d.data) + offset;
  AudioChunk chunk;
  for (uint32_t i = 0; i < n;) {
    const uint32_t take = std::min<uint32_t>(n - i, 64);
    for (uint32_t k = 0; k < take; ++k)
      std::memcpy(&chunk.s[k], base + static_cast<size_t>(i + k) * stride, sizeof(float));
    chunk.len = take;
    emit(chunk);
    i += take;
  }
}

// --- MIDI: a control sequence of raw MIDI or UMP ------------------------------

// CLOCK_MONOTONIC ns at which a control `offset` frames into this cycle
// sounds: one cycle after its capture time (the latency the scheduler in
// RtRunner adds back). 0, meaning "next block", when the clock is unknown.
inline uint64_t control_time_ns(const spa_io_position* p, uint32_t offset) {
  if (p == nullptr || p->clock.rate.denom == 0) return 0;
  const double ns_per_frame = 1e9 * p->clock.rate.num / p->clock.rate.denom;
  return p->clock.nsec +
         static_cast<uint64_t>(static_cast<double>(offset + p->clock.duration) * ns_per_frame);
}

// Every MIDI 1.0 event in a control sequence pod, raw MIDI and UMP alike,
// to `emit(event, at_ns)`. `ump_state` carries UMP sysex state across calls.
// A pod that is not a sequence emits nothing.
template <class Emit>
void midi_from_pod(const spa_pod* pod, const spa_io_position* p, uint64_t& ump_state,
                   Emit&& emit) {
  if (pod == nullptr || spa_pod_is_sequence(pod) == 0) return;
  const auto* seq = reinterpret_cast<const spa_pod_sequence*>(pod);
  const spa_pod_control* c;
  SPA_POD_SEQUENCE_FOREACH(seq, c) {
    const uint64_t at = control_time_ns(p, c->offset);
    auto raw = [&](const uint8_t* bytes, size_t len) {
      io::parse_midi_bytes(bytes, len, [&](const dsp::MidiEvent& e) { emit(e, at); });
    };
    if (c->type == SPA_CONTROL_Midi) {
      raw(static_cast<const uint8_t*>(SPA_POD_BODY(&c->value)), SPA_POD_BODY_SIZE(&c->value));
    } else if (c->type == SPA_CONTROL_UMP) {
      const auto* words = static_cast<const uint32_t*>(SPA_POD_BODY(&c->value));
      size_t remaining = SPA_POD_BODY_SIZE(&c->value);
      while (remaining >= 4) {
        uint8_t midi[8];
        const int n = spa_ump_to_midi(&words, &remaining, midi, sizeof(midi), &ump_state);
        if (n < 0) break;  // malformed or oversized packet: drop the rest
        if (n > 0) raw(midi, static_cast<size_t>(n));
      }
    }
  }
}

}  // namespace ms::engine::pw
