// The live PipeWire path's buffer handling (engine/pw_buffers.h), driven with
// hand-built spa buffers and pods: what each callback reads from and writes
// to a buffer, without a running daemon.
#include <doctest/doctest.h>
#include <cstdint>
#include <cstring>
#include <vector>

#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wconversion"
#pragma GCC diagnostic ignored "-Wsign-conversion"
#pragma GCC diagnostic ignored "-Wold-style-cast"
#pragma GCC diagnostic ignored "-Wmissing-field-initializers"
#include <spa/pod/builder.h>
#pragma GCC diagnostic pop

#include "engine/pw_buffers.h"

using namespace ms;
using namespace ms::engine;

namespace {

// One or two float planes with their chunk headers, as PipeWire hands them.
struct Planes {
  std::vector<float> mem[2];
  spa_chunk chunk[2]{};
  spa_data data[2]{};
  spa_buffer buf{};
  Planes(uint32_t n_datas, uint32_t floats0, uint32_t floats1 = 0) {
    const uint32_t sizes[2] = {floats0, floats1};
    for (uint32_t c = 0; c < n_datas; ++c) {
      mem[c].assign(sizes[c], 0.0f);
      data[c].data = mem[c].data();
      data[c].maxsize = sizes[c] * static_cast<uint32_t>(sizeof(float));
      data[c].chunk = &chunk[c];
    }
    buf.n_datas = n_datas;
    buf.datas = data;
  }
};

struct Midi {
  dsp::MidiEvent e;
  uint64_t at;
};

// A control sequence: (offset, type, body bytes) per control.
struct Control {
  uint32_t offset, type;
  std::vector<uint8_t> body;
};

std::vector<Midi> midi_of(const std::vector<Control>& controls, const spa_io_position* pos,
                          uint64_t& ump_state) {
  alignas(8) uint8_t mem[1024];
  spa_pod_builder b{};
  spa_pod_builder_init(&b, mem, sizeof mem);
  spa_pod_frame f{};
  spa_pod_builder_push_sequence(&b, &f, 0);
  for (const auto& c : controls) {
    spa_pod_builder_control(&b, c.offset, c.type);
    spa_pod_builder_bytes(&b, c.body.data(), static_cast<uint32_t>(c.body.size()));
  }
  const auto* pod = static_cast<const spa_pod*>(spa_pod_builder_pop(&b, &f));
  std::vector<Midi> out;
  pw::midi_from_pod(pod, pos, ump_state,
                    [&](const dsp::MidiEvent& e, uint64_t at) { out.push_back({e, at}); });
  return out;
}

std::vector<uint8_t> words(std::initializer_list<uint32_t> w) {
  std::vector<uint8_t> b(w.size() * 4);
  std::memcpy(b.data(), std::data(w), b.size());
  return b;
}

}  // namespace

TEST_CASE("playback: frames follow the request, bounded by the smaller plane") {
  Planes p(2, 512, 256);
  CHECK(pw::playback_frames(p.buf, 128) == 128);
  CHECK(pw::playback_frames(p.buf, 4096) == 256);  // never past either plane
  CHECK(pw::playback_frames(p.buf, 0) == 256);     // no request: fill it
  pw::mark_planes(p.buf, 200);
  for (int c = 0; c < 2; ++c) {
    CHECK(p.chunk[c].offset == 0);
    CHECK(p.chunk[c].stride == 4);
    CHECK(p.chunk[c].size == 800);
  }
  Planes mono(1, 512);
  CHECK(pw::playback_frames(mono.buf, 128) == 0);  // one plane: write nothing
  p.data[1].data = nullptr;
  CHECK(pw::playback_frames(p.buf, 128) == 0);
}

TEST_CASE("capture: chunks of 64, in order, from the chunk's offset") {
  Planes p(1, 200);
  for (uint32_t i = 0; i < 200; ++i) p.mem[0][i] = static_cast<float>(i);
  p.chunk[0].offset = 8;   // two floats in
  p.chunk[0].size = 150 * 4;
  p.chunk[0].stride = 4;
  std::vector<uint32_t> lens;
  std::vector<float> got;
  pw::capture_chunks(p.buf, [&](const AudioChunk& c) {
    lens.push_back(c.len);
    got.insert(got.end(), c.s, c.s + c.len);
  });
  CHECK(lens == std::vector<uint32_t>{64, 64, 22});
  REQUIRE(got.size() == 150);
  CHECK(got.front() == 2.0f);
  CHECK(got.back() == 151.0f);
}

TEST_CASE("capture: a peer's bad chunk header never reads past the plane") {
  Planes p(1, 100);
  for (uint32_t i = 0; i < 100; ++i) p.mem[0][i] = static_cast<float>(i);
  auto count = [&] {
    size_t n = 0;
    pw::capture_chunks(p.buf, [&](const AudioChunk& c) { n += c.len; });
    return n;
  };
  p.chunk[0] = {0, 4000, 4, 0};  // size past maxsize
  CHECK(count() == 100);
  p.chunk[0] = {396, 400, 4, 0};  // offset at the last float
  CHECK(count() == 1);
  p.chunk[0] = {500, 400, 4, 0};  // offset past the end
  CHECK(count() == 0);
  p.chunk[0] = {0, 400, 1, 0};  // stride under a float: read floats, not bytes
  CHECK(count() == 100);
  p.chunk[0] = {0, 400, 0, 0};  // no stride given
  CHECK(count() == 100);
  // A wider stride (interleaved stereo) takes the first channel of each frame.
  p.chunk[0] = {0, 400, 8, 0};
  std::vector<float> got;
  pw::capture_chunks(p.buf, [&](const AudioChunk& c) { got.insert(got.end(), c.s, c.s + c.len); });
  REQUIRE(got.size() == 50);
  CHECK(got[1] == 2.0f);
  CHECK(got[49] == 98.0f);
  p.data[0].data = nullptr;
  CHECK(count() == 0);
}

TEST_CASE("midi: raw and UMP controls, timed one cycle after capture") {
  spa_io_position pos{};
  pos.clock.rate = SPA_FRACTION(1, 48000);
  pos.clock.nsec = 1'000'000'000;
  pos.clock.duration = 480;  // 10 ms cycle
  uint64_t ump = 0;
  const auto ev = midi_of(
      {{0, SPA_CONTROL_Midi, {0x90, 60, 100, 62, 100}},  // running status: two notes
       {48, SPA_CONTROL_UMP, words({0x20903c64u})},      // MIDI 1.0 note on, group 0
       {96, SPA_CONTROL_UMP, words({0x2080'3c00u, 0x20b0'0740u})},
       {100, SPA_CONTROL_Properties, {1, 2, 3}}},        // not MIDI: ignored
      &pos, ump);
  REQUIRE(ev.size() == 5);
  CHECK(ev[1].e.data[0] == 0x90);
  CHECK(ev[1].e.data[1] == 62);
  CHECK(ev[0].at == 1'010'000'000);  // capture time + one cycle
  CHECK(ev[2].e.data[0] == 0x90);
  CHECK(ev[2].e.data[1] == 60);
  CHECK(ev[2].e.data[2] == 100);
  CHECK(ev[2].at == 1'011'000'000);  // 48 frames = 1 ms later
  CHECK(ev[3].e.data[0] == 0x80);
  CHECK(ev[4].e.data[0] == 0xb0);
  CHECK(ev[4].e.data[1] == 7);
  CHECK(ev[4].e.data[2] == 0x40);
  CHECK(ev[4].at == 1'012'000'000);

  // Unknown clock: every event is "now" (0), for the next block.
  for (const auto& m : midi_of({{10, SPA_CONTROL_Midi, {0x80, 60, 0}}}, nullptr, ump))
    CHECK(m.at == 0);
  // A truncated UMP word drops quietly.
  CHECK(midi_of({{0, SPA_CONTROL_UMP, {0x64, 0x3c}}}, &pos, ump).empty());
  // Not a sequence: nothing.
  const spa_pod none{0, SPA_TYPE_None};
  size_t n = 0;
  pw::midi_from_pod(&none, &pos, ump, [&](const dsp::MidiEvent&, uint64_t) { ++n; });
  pw::midi_from_pod(nullptr, &pos, ump, [&](const dsp::MidiEvent&, uint64_t) { ++n; });
  CHECK(n == 0);
}
