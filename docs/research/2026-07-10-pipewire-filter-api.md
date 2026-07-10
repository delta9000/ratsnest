# PipeWire `pw_filter` Engineering Brief — Realtime Stereo Audio Source

Verified against local `libpipewire-0.3` **1.6.7** (`/usr/include/pipewire-0.3/pipewire/filter.h`, `thread-loop.h`, `keys.h`, `data-loop.h`; `/usr/include/spa-0.2/spa/node/io.h`), the official `audio-dsp-src.c` example, and a real `g++ -std=c++20 -Wall -Wextra -Wconversion -Werror` compile test on this machine.

## 1. Minimal filter lifecycle for an audio source

```
pw_init(&argc, &argv);
loop = pw_thread_loop_new("modsynth-audio", NULL);
pw_thread_loop_lock(loop);
filter = pw_filter_new_simple(pw_thread_loop_get_loop(loop), "modsynth",
    pw_properties_new(PW_KEY_MEDIA_TYPE, "Audio",
                       PW_KEY_MEDIA_CATEGORY, "Playback",
                       PW_KEY_MEDIA_ROLE, "Music",
                       PW_KEY_MEDIA_CLASS, "Stream/Output/Audio",
                       PW_KEY_NODE_AUTOCONNECT, "true", NULL),
    &filter_events, &data);
port_l = pw_filter_add_port(filter, PW_DIRECTION_OUTPUT, PW_FILTER_PORT_FLAG_MAP_BUFFERS,
    sizeof(struct port), pw_properties_new(PW_KEY_FORMAT_DSP, "32 bit float mono audio",
    PW_KEY_PORT_NAME, "out_l", PW_KEY_AUDIO_CHANNEL, "FL", NULL), NULL, 0);
port_r = /* same, "out_r", "FR" */;
pw_filter_connect(filter, PW_FILTER_FLAG_RT_PROCESS, NULL, 0);
pw_thread_loop_unlock(loop);
pw_thread_loop_start(loop);
```

Use `pw_thread_loop` (not `pw_main_loop`) — it spawns its own thread so the CLI/main thread stays free. **All** `pw_*` calls on objects belonging to this loop, from any thread other than the loop's own, must be wrapped in `pw_thread_loop_lock()`/`unlock()`; the lock is recursive. Filter/loop event callbacks (state_changed, param_changed) are delivered with the lock already held — the `process` callback is the sole documented exception (`thread-loop.h:60-68`).

Note: the shipped `audio-dsp-src.c` example uses `PW_KEY_MEDIA_CATEGORY "Source"`, which isn't one of the canonical values (`keys.h:345-346`: "Playback, Capture, Duplex, Monitor, Manager"). What drives WirePlumber's autoconnect-to-default-sink policy is `PW_KEY_MEDIA_CLASS "Stream/Output/Audio"` + `PW_KEY_NODE_AUTOCONNECT "true"`; use `MEDIA_CATEGORY "Playback"`.

Ports: `pw_filter_add_port()` (`filter.h:185-192`) with `PW_DIRECTION_OUTPUT`, `PW_FILTER_PORT_FLAG_MAP_BUFFERS`, port props `PW_KEY_FORMAT_DSP="32 bit float mono audio"` (DSP ports are always mono → stereo = 2 ports), `PW_KEY_PORT_NAME`, `PW_KEY_AUDIO_CHANNEL "FL"/"FR"`. No format params array needed (example passes `NULL, 0`).

`PW_FILTER_FLAG_RT_PROCESS` (`filter.h:106-108`): "call process from the realtime thread. Only call methods marked as RT safe." Without it, `process` runs on the thread-loop thread.

`on_process(void *userdata, struct spa_io_position *position)` (`filter.h:87`) — per-port buffer via `void *pw_filter_get_dsp_buffer(void *port_data, uint32_t n_samples)` (`filter.h:241`, RT-safe). **It can return NULL** (port not yet negotiated/no buffer) — always null-check and skip the cycle.

## 2. Quantum and sample rate

`n_samples = position->clock.duration` (`spa/node/io.h:176`) — samples for *this* cycle; **can vary call-to-call** (adaptive quantum) unless pinned via `PW_KEY_NODE_FORCE_QUANTUM`; never cache outside the callback. Sample rate: `position->clock.rate` (`spa_fraction`, typically `{1, 48000}`) — re-read each call; no param_changed needed. `SPA_IO_CLOCK_FLAG_DISCONT` (`io.h:151-164`) signals a timestamp discontinuity.

## 3. Threading and RT guarantees

With `RT_PROCESS`, `process()` runs on PipeWire's internal **data loop** (`data-loop.h:15-21`), a dedicated RT thread the client library spawns (distinct from the pw_thread_loop thread). `pw_filter_get_data_loop()` exposes it post-connect. SCHED_FIFO priority comes automatically via `libpipewire-module-rt` in client.conf (RLIMIT_RTPRIO, else Portal Realtime / RTKit, default SCHED_FIFO 20). No client code needed; opt-out not opt-in.

In `process()`: only RT-safe-marked functions (`pw_filter_get_dsp_buffer`, `dequeue/queue_buffer`, `pw_filter_get_nsec`, `pw_filter_trigger_process`, `pw_filter_emit_event`). Never allocate, lock, log, do I/O, or call non-RT-marked `pw_*`.

## 4. State/error handling and teardown

`state_changed(void *data, enum pw_filter_state old, enum pw_filter_state state, const char *error)` — states: `ERROR(-1)`, `UNCONNECTED(0)`, `CONNECTING(1)`, `PAUSED(2)`, `STREAMING(3)`. Since PipeWire 1.4, ERROR also sets errno. Fires on the thread-loop thread with the lock held — safe hook for non-RT state/reporting. Server disconnect → transition to ERROR or UNCONNECTED. No xrun callback on pw_filter — xruns visible via `SPA_IO_CLOCK_FLAG_XRUN_RECOVER` on `position->clock.flags` and `clock.xrun` (accumulated duration).

Teardown order (per `thread-loop.h:36-42`, shairport-sync `audio_pw.c`):
```
pw_thread_loop_stop(loop);      // WITHOUT the lock held
pw_filter_destroy(filter);      // loop thread stopped, no lock needed
pw_thread_loop_destroy(loop);
pw_deinit();
```

## 5. CMake integration

```cmake
find_package(PkgConfig REQUIRED)
pkg_check_modules(PIPEWIRE REQUIRED IMPORTED_TARGET libpipewire-0.3)
target_link_libraries(<target> PRIVATE PkgConfig::PIPEWIRE)
```
`PkgConfig::PIPEWIRE` propagates `-I` and `-D_REENTRANT -fno-strict-aliasing -fno-strict-overflow` (verified locally).

**Warnings-as-errors gotcha (verified by compiling here):** raw `#include <pipewire/pipewire.h>` under `-Wall -Wextra -Wconversion -Werror -std=c++20` **fails** — `spa/utils/string.h`, `spa/utils/json-core.h`, `pipewire/map.h` trip `-Wconversion`. Fix (compiled clean at ci flags):
```cpp
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wconversion"
#pragma GCC diagnostic ignored "-Wsign-conversion"
#include <pipewire/pipewire.h>
#include <pipewire/filter.h>
#pragma GCC diagnostic pop
```
Confine to a single translation unit (e.g. `audio_backend.cpp`) exposing a narrow C++ interface.

## 6. Skeleton: stereo sine source (verified shape)

```cpp
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wconversion"
#pragma GCC diagnostic ignored "-Wsign-conversion"
#include <pipewire/pipewire.h>
#include <pipewire/filter.h>
#pragma GCC diagnostic pop

#include <cmath>
#include <cstdio>

namespace {
constexpr float kTwoPi = 6.28318530717958647692f;
constexpr float kFreq = 440.0f;

struct Port { float phase = 0.0f; };
struct Data {
    pw_thread_loop *loop = nullptr;
    pw_filter *filter = nullptr;
    Port *out_l = nullptr;
    Port *out_r = nullptr;
};

void on_process(void *userdata, spa_io_position *position) {
    auto *data = static_cast<Data *>(userdata);
    uint32_t n_samples = position->clock.duration;
    float inc = kTwoPi * kFreq *
        static_cast<float>(position->clock.rate.num) /
        static_cast<float>(position->clock.rate.denom);

    float *l = static_cast<float *>(pw_filter_get_dsp_buffer(data->out_l, n_samples));
    float *r = static_cast<float *>(pw_filter_get_dsp_buffer(data->out_r, n_samples));
    if (l == nullptr || r == nullptr)
        return; // port not ready this cycle — must not crash

    float phase = data->out_l->phase;
    for (uint32_t i = 0; i < n_samples; ++i) {
        phase += inc;
        if (phase >= kTwoPi) phase -= kTwoPi;
        float s = std::sin(phase) * 0.5f;
        l[i] = s;
        r[i] = s;
    }
    data->out_l->phase = phase;
}

const pw_filter_events kFilterEvents = {
    PW_VERSION_FILTER_EVENTS,
    .state_changed = [](void *, pw_filter_state, pw_filter_state, const char *) {},
    .process = on_process,
};
} // namespace

int main(int argc, char *argv[]) {
    Data data{};
    pw_init(&argc, &argv);
    data.loop = pw_thread_loop_new("modsynth-audio", nullptr);

    pw_thread_loop_lock(data.loop);
    data.filter = pw_filter_new_simple(
        pw_thread_loop_get_loop(data.loop), "modsynth",
        pw_properties_new(
            PW_KEY_MEDIA_TYPE, "Audio",
            PW_KEY_MEDIA_CATEGORY, "Playback",
            PW_KEY_MEDIA_ROLE, "Music",
            PW_KEY_MEDIA_CLASS, "Stream/Output/Audio",
            PW_KEY_NODE_AUTOCONNECT, "true",
            PW_KEY_NODE_LATENCY, "256/48000",
            nullptr),
        &kFilterEvents, &data);

    data.out_l = static_cast<Port *>(pw_filter_add_port(
        data.filter, PW_DIRECTION_OUTPUT, PW_FILTER_PORT_FLAG_MAP_BUFFERS,
        sizeof(Port),
        pw_properties_new(PW_KEY_FORMAT_DSP, "32 bit float mono audio",
                           PW_KEY_PORT_NAME, "out_l",
                           PW_KEY_AUDIO_CHANNEL, "FL", nullptr),
        nullptr, 0));
    data.out_r = static_cast<Port *>(pw_filter_add_port(
        data.filter, PW_DIRECTION_OUTPUT, PW_FILTER_PORT_FLAG_MAP_BUFFERS,
        sizeof(Port),
        pw_properties_new(PW_KEY_FORMAT_DSP, "32 bit float mono audio",
                           PW_KEY_PORT_NAME, "out_r",
                           PW_KEY_AUDIO_CHANNEL, "FR", nullptr),
        nullptr, 0));

    if (pw_filter_connect(data.filter, PW_FILTER_FLAG_RT_PROCESS, nullptr, 0) < 0) {
        std::fprintf(stderr, "pw_filter_connect failed\n");
        return 1;
    }
    pw_thread_loop_unlock(data.loop);
    pw_thread_loop_start(data.loop);

    // ... run CLI / render loop on this (main) thread ...
    std::getchar();

    pw_thread_loop_stop(data.loop);   // no lock held
    pw_filter_destroy(data.filter);
    pw_thread_loop_destroy(data.loop);
    pw_deinit();
    return 0;
}
```

## 7. Latency/quantum as CLI flags

`PW_KEY_NODE_LATENCY` (e.g. `"256/48000"`) — natural `--latency` CLI flag; a *request*, negotiated with the driver. `PW_KEY_NODE_FORCE_QUANTUM` pins the graph quantum (affects every client — debug flag only). Also available: `PW_KEY_NODE_RATE`/`PW_KEY_NODE_FORCE_RATE` for `--sample-rate`, `PW_KEY_TARGET_OBJECT` for `--output-device`.

Sources: docs.pipewire.org (pw_filter group, pw_filter_events, module-rt, thread_loop, Tutorial 7), audio-dsp-src.c example, local headers (libpipewire-0.3 1.6.7).

## Addendum (2026-07-10): pw_stream switch

Everything above is accurate and was compiled/run — but it describes an API
that WirePlumber 0.5.15 cannot auto-route. Confirmed empirically (a working
`pw_filter` node never gained links to the default sink) and in WP source:
`create-item.lua`'s default policy dispatches purely on the node's
`media.class` string ("Audio" → `si-audio-adapter`), and `si-audio-adapter`
requires a node-level `EnumFormat` param to negotiate a format before it will
link. `pw_filter` nodes only expose per-port DSP formats (always mono float,
fixed), not a node-level `EnumFormat`, so `si-audio-adapter` never completes
adapter setup for them — the node sits connectable-but-unrouted forever,
regardless of `PW_KEY_NODE_AUTOCONNECT`/`MEDIA_CLASS`.

`pw_stream` nodes go through the client-node adapter machinery and do expose
a node-level `EnumFormat` (built here via `spa_format_audio_raw_build`,
`SPA_AUDIO_FORMAT_F32P`, 2 channels FL/FR), so WirePlumber's ordinary
audio-adapter policy links them to the default sink automatically. A
`pw_stream` prototype (`stream_sine.c`) verified this on this machine: format
negotiated to rate=48000, links appeared in `pw-link -l`, and audio was
audible.

Decision: rework `src/engine/pipewire_out.cpp` from `pw_filter` to
`pw_stream`. The RT discipline, thread-loop lock ordering, teardown sequence,
and `-Wconversion` pragma wrapper documented above all still apply verbatim
to `pw_stream` — only the node/port/format API surface changes. Two notable
API differences: `pw_stream`'s `process` callback takes no `spa_io_position`
argument directly (the graph clock/rate is instead obtained via the
`io_changed` event with `id == SPA_IO_Position`, or via `param_changed` with
`id == SPA_PARAM_Format` for the negotiated sample rate through
`spa_format_audio_raw_parse`); and buffers are dequeued/queued explicitly
(`pw_stream_dequeue_buffer`/`pw_stream_queue_buffer`) against planar
(F32P) `spa_data` slots rather than fetched per-port via
`pw_filter_get_dsp_buffer`.

This document predates the `pw_stream` switch: everything above the addendum
describes the abandoned `pw_filter` approach and should not be read as current
guidance — see spec §7.2 for the implemented backend.
