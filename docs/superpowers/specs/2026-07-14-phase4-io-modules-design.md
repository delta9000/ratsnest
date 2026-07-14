# Phase 4 — sound-design I/O modules design

**Status:** design 2026-07-14. Completes the spec's Phase 4 (`docs/superpowers/specs/2026-07-10-modsynth-design.md` §5). `follow` already shipped; this covers the three I/O members: `sampler`, `rec`, `extin`, plus the shared WAV reader and the audio-capture engine hook `extin` needs.

**Autonomy note:** authored without user Q&A at the user's explicit request ("build out the rest autonomously, no questions"). Every fork below was decided against the project's non-negotiable rules; the rationale is recorded here in lieu of a brainstorming dialogue.

## Design rules that bind this work

- **Rule 3 (RT safety):** `process()` and anything reachable on the audio thread never allocate, lock, or do I/O. File reads happen in `init()` (non-RT); file writes happen at teardown (non-RT). `copy_state_from` is RT-called, so it copies only scalar playback state — never buffers.
- **Rule 7 (deps):** PipeWire + doctest only. The WAV reader is hand-written in `ms::io`; no codec dependency is added.
- **Rule 6 (errors never kill the session):** a missing/malformed WAV yields a silent module, not a crash. The last good graph keeps playing.
- **Rule 8 (signal conventions):** audio ±1, gates rise through 0.5.

## Shared: WAV reader (`src/io/wav.{h,cpp}`)

Add a reader beside the existing `write_wav_f32`:

```cpp
struct WavData {
  std::vector<float> samples;  // interleaved, normalized to ~[-1, 1]
  int channels = 0;
  int sample_rate = 0;
  size_t frames = 0;           // samples.size() / channels
};
bool read_wav_f32(const std::string& path, WavData& out);
```

Parses canonical RIFF/WAVE: reads the `RIFF`/`WAVE` tags, then walks chunks, honoring each chunk's declared size and skipping unknown chunks (chunk bodies are word-aligned — odd sizes carry a pad byte). Reads `fmt ` (format tag, channels, rate, bits/sample) and `data`. Supports:
- **PCM integer** (tag 1): 16-bit (`/32768`), 24-bit (packed 3-byte LE `/2^23`), 32-bit (`/2^31`).
- **IEEE float** (tag 3): 32-bit (copied through).
- **WAVE_FORMAT_EXTENSIBLE** (tag `0xFFFE`): the real format is the first two bytes of the `SubFormat` GUID in the `fmt ` extension; treat as tag 1 or 3 accordingly.

Returns `false` (and leaves `out` empty) on: open failure, missing/short header, unsupported tag/bit-depth, or truncated `data`. Non-RT (`fopen`/`fread`). This is `sampler`'s only new infrastructure; `rec` reuses `write_wav_f32` unchanged.

## `sampler` — file playback

```text
sampler { file: "kick.wav", loop: off }
  inputs:  trig 0, pitch 0, level 1
  config:  file (string, any), loop = off | on   (default off)
  outputs: out, eos
```

- **Load at `init()`:** `read_wav_f32(file)`; mix any channel count to a single mono buffer (average of channels). Store the file's own sample rate. A failed load leaves an empty buffer → the module is silent (rule 6).
- **Pitch/rate:** advance the fractional read position by `step = (file_rate / engine_rate) * 2^pitch` samples per output sample, so a file recorded at any rate plays back at correct pitch/speed, and `+1 v/oct` doubles it. `pitch` is summed CV in v/oct (convention).
- **`trig`:** a rising edge (through 0.5) resets the read position to 0 and (re)starts playback. With nothing patched to `trig`, playback starts once at graph load and (in `loop: off`) plays through a single time.
- **Read:** linear interpolation between the two samples straddling the fractional position, scaled by `level`.
- **End:** in `loop: on`, wrap the position modulo the buffer length. In `loop: off`, stop at the end (`out = 0`), and emit a one-sample `eos` pulse (1.0) on the transition.
- **`copy_state_from`:** migrate the read position and playing/gate flags only — never the sample buffer (RT rule). The new instance already loaded its own buffer in `init()`.

## `rec` — tap any point to WAV

```text
rec { file: "take.wav", secs: 30 }
  inputs:  in_l 0, in_r 0, gate 1
  config:  file (string), secs (number, capture cap; default 30, clamped 1..600)
  outputs: (none — a sink, like scope)
```

- **Allocate at `init()`:** an interleaved stereo capture buffer of `secs * engine_rate` frames. Store the path, rate, capacity, and `count_ = 0`.
- **`process()`:** while `gate > 0.5` and `count_ < capacity`, append `(in_l, in_r)` and increment `count_`. Pure array writes — no alloc, no I/O. `gate` defaults to 1, so an unpatched `rec` records continuously until the cap. Beyond the cap, capture stops (no wrap — a WAV has a finite length; document the cap).
- **Write at teardown:** the destructor calls `write_wav_f32(file, buffer, count_, 2, rate)` when `count_ > 0` (non-RT). For an offline `render`, the engine is destroyed at the end of the render, producing exactly one clean take alongside the main mix.
- **Hot reload:** the capture buffer is **not** migrated (memcpy of an 11 MB buffer on the RT thread would xrun — rule 3). Editing the patch while recording therefore restarts the take: the outgoing instance writes its take-so-far, the new instance records afresh and overwrites on its own teardown. Last generation wins. Documented as a known behavior; offline render (no reload) is unaffected.

## `extin` — external audio capture

The only member needing engine changes. It mirrors the MIDI-in bridge exactly (`docs/superpowers/specs/2026-07-11-midi-design.md`).

```text
extin
  inputs:  (none)
  outputs: out    (mono captured audio, ±1)
```

**Engine hook (mirrors `wants_midi`):**
1. `ModuleInfo::wants_audio_in` flag + `Module::on_audio_in(const float* samples, size_t n)` virtual (default no-op), beside the MIDI equivalents.
2. `graph::Graph::audio_in_nodes`, populated once in the `Engine` constructor (mirrors `midi_nodes`); `Engine::deliver_audio(const float*, size_t)` fans a chunk to each opted-in node.
3. `RtRunner`: an SPSC ring `audio_ring_` of fixed `AudioChunk { float s[64]; uint8_t len; }` (trivially copyable), a `push_audio(...)` producer, and a drain in `render_block` before `process()` (mirrors the MIDI drain).
4. A new `AudioIn` PipeWire capture class (`pw_stream`, `PW_DIRECTION_INPUT`, `MEDIA_CATEGORY=Capture`, `F32P`) on its own thread-loop, whose RT `on_process` pushes captured samples as chunks to `runner.push_audio(...)`. Constructed once in `cmd_play` beside `MidiIn`; non-fatal on failure.
5. The `extin` module buffers samples delivered via `on_audio_in` into a small internal ring (both `on_audio_in` and `process` run on the audio thread, so no atomics are needed inside the module) and emits one sample per `process()` call; underrun outputs 0. `copy_state_from` migrates the internal ring contents so a hot reload doesn't glitch the capture.

**Continuous-audio caveat (vs sparse MIDI):** capture and playback clocks drift, so the internal ring carries ~1–2 blocks of slack; overrun drops oldest, underrun emits 0. Latency is ~one audio block plus that slack. This is the one place the MIDI template's harmless block-boundary quantization doesn't transfer cleanly, and it is an accepted tradeoff for a personal instrument.

**Verification boundary (stated plainly):** like `midi`, `extin` produces silence under offline `render` (the capture stream is never constructed there), so end-to-end live capture **cannot be verified in this environment** — no live audio session or capture device is available to the build. What *is* verified: the module's ring/buffering logic (unit tests feeding `on_audio_in` and checking `process` output, including underrun), offline silence, and that the full play path compiles and wires up under all gates (`test`, `test-san`, `ci`). Live capture correctness is left for the user to confirm on hardware.

## Paths

`file:` strings resolve against the process working directory (mise runs from the repo root), matching how the CLI resolves patch and `-o` paths today. No patch-directory-relative resolution exists and threading one through `compile()`→`init()` is out of scope; absolute paths always work. Documented in `docs/modules.md`.

## Testing strategy

- **WAV reader:** round-trip via the existing `write_wav_f32` (write float32 → read → compare); plus hand-built PCM16 and PCM24 byte buffers to exercise integer decoding; malformed/missing files return false.
- **sampler:** generate a ramp/impulse fixture with `write_wav_f32` to a temp path, load via `file:`, and check: plays the samples back, `pitch` doubles the rate, `loop: on` wraps, `loop: off` emits `eos` and then silence, `trig` restarts. Fixtures use absolute temp paths (ctest's CWD is the build dir).
- **rec:** run a known signal through it into a temp path, destroy the module, read the file back with the new reader, and assert the captured samples match; `gate` gating and the `secs` cap are checked; empty capture writes nothing.
- **extin:** feed `on_audio_in` chunks and assert `process` emits them in order; underrun emits 0; `copy_state_from` preserves pending samples. Offline render of a patch using `extin` is silent and finite.

## Non-goals

Sample-accurate capture sync, resampling in `extin`, multi-channel `extin`, `rec` ring/overwrite modes, sample-start/loop-point config on `sampler`, and patch-relative path resolution. All deferred.
