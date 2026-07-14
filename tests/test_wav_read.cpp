#include <doctest/doctest.h>
#include <cstdint>
#include <cstdio>
#include <filesystem>
#include <vector>
#include "io/wav.h"

using namespace ms::io;

namespace {
// Write raw bytes to a temp file and return its path.
std::filesystem::path write_bytes(const char* name, const std::vector<uint8_t>& b) {
  auto path = std::filesystem::temp_directory_path() / name;
  std::FILE* f = std::fopen(path.string().c_str(), "wb");
  REQUIRE(f != nullptr);
  std::fwrite(b.data(), 1, b.size(), f);
  std::fclose(f);
  return path;
}

void put_u32(std::vector<uint8_t>& b, uint32_t v) {
  b.push_back(static_cast<uint8_t>(v & 0xffu));
  b.push_back(static_cast<uint8_t>((v >> 8) & 0xffu));
  b.push_back(static_cast<uint8_t>((v >> 16) & 0xffu));
  b.push_back(static_cast<uint8_t>((v >> 24) & 0xffu));
}
void put_u16(std::vector<uint8_t>& b, uint16_t v) {
  b.push_back(static_cast<uint8_t>(v & 0xffu));
  b.push_back(static_cast<uint8_t>((static_cast<uint32_t>(v) >> 8) & 0xffu));
}
void put_tag(std::vector<uint8_t>& b, const char* t) {
  for (int i = 0; i < 4; ++i) b.push_back(static_cast<uint8_t>(t[i]));
}

// Build a canonical WAV: format tag, bits/sample, channels, rate, and a raw
// data-chunk body (already encoded to that format).
std::vector<uint8_t> build_wav(uint16_t fmt, uint16_t bits, uint16_t channels,
                               uint32_t rate, const std::vector<uint8_t>& data) {
  std::vector<uint8_t> b;
  const uint16_t block_align = static_cast<uint16_t>(channels * (bits / 8));
  put_tag(b, "RIFF");
  put_u32(b, static_cast<uint32_t>(36 + data.size()));
  put_tag(b, "WAVE");
  put_tag(b, "fmt ");
  put_u32(b, 16);
  put_u16(b, fmt);
  put_u16(b, channels);
  put_u32(b, rate);
  put_u32(b, rate * block_align);  // byte rate
  put_u16(b, block_align);
  put_u16(b, bits);
  put_tag(b, "data");
  put_u32(b, static_cast<uint32_t>(data.size()));
  b.insert(b.end(), data.begin(), data.end());
  return b;
}
}  // namespace

TEST_CASE("read_wav_f32: round-trips a float32 stereo file written by write_wav_f32") {
  auto path = std::filesystem::temp_directory_path() / "modsynth_rt_f32.wav";
  const float in[8] = {0.f, 0.f, 0.5f, -0.5f, 1.f, -1.f, 0.25f, -0.75f};  // 4 frames
  REQUIRE(write_wav_f32(path.string(), in, 4, 2, 44100));

  WavData w;
  REQUIRE(read_wav_f32(path.string(), w));
  std::filesystem::remove(path);

  CHECK(w.channels == 2);
  CHECK(w.sample_rate == 44100);
  CHECK(w.frames == 4);
  REQUIRE(w.samples.size() == 8);
  for (size_t i = 0; i < 8; ++i) CHECK(w.samples[i] == doctest::Approx(in[i]));
}

TEST_CASE("read_wav_f32: decodes 16-bit PCM normalized to [-1, 1]") {
  // Mono, 3 samples: 0, +full, -full.
  std::vector<uint8_t> data;
  put_u16(data, 0);        // 0.0
  put_u16(data, 0x7fff);   // ~+1.0
  put_u16(data, 0x8000);   // -1.0 (int16 min)
  auto bytes = build_wav(/*fmt=*/1, /*bits=*/16, /*channels=*/1, /*rate=*/48000, data);
  auto path = write_bytes("modsynth_pcm16.wav", bytes);

  WavData w;
  REQUIRE(read_wav_f32(path.string(), w));
  std::filesystem::remove(path);

  CHECK(w.channels == 1);
  CHECK(w.sample_rate == 48000);
  REQUIRE(w.samples.size() == 3);
  CHECK(w.samples[0] == doctest::Approx(0.0f));
  CHECK(w.samples[1] == doctest::Approx(1.0f).epsilon(0.001));
  CHECK(w.samples[2] == doctest::Approx(-1.0f).epsilon(0.001));
}

TEST_CASE("read_wav_f32: decodes 24-bit PCM") {
  // Mono, 2 samples: 0, +full (0x7fffff). 24-bit little-endian packed.
  std::vector<uint8_t> data = {
      0x00, 0x00, 0x00,   // 0.0
      0xff, 0xff, 0x7f,   // ~+1.0
  };
  auto bytes = build_wav(/*fmt=*/1, /*bits=*/24, /*channels=*/1, /*rate=*/48000, data);
  auto path = write_bytes("modsynth_pcm24.wav", bytes);

  WavData w;
  REQUIRE(read_wav_f32(path.string(), w));
  std::filesystem::remove(path);

  REQUIRE(w.samples.size() == 2);
  CHECK(w.samples[0] == doctest::Approx(0.0f));
  CHECK(w.samples[1] == doctest::Approx(1.0f).epsilon(0.001));
}

TEST_CASE("read_wav_f32: skips unknown chunks before data") {
  // Insert a "LIST" chunk between fmt and data by hand-building.
  std::vector<uint8_t> data;
  put_u16(data, 0x4000);  // +0.5
  std::vector<uint8_t> b;
  put_tag(b, "RIFF");
  // sizes computed after assembly are not validated by the reader beyond bounds
  put_u32(b, 0);  // placeholder RIFF size (reader walks chunks, not this field)
  put_tag(b, "WAVE");
  put_tag(b, "fmt ");
  put_u32(b, 16);
  put_u16(b, 1); put_u16(b, 1); put_u32(b, 48000); put_u32(b, 96000);
  put_u16(b, 2); put_u16(b, 16);
  put_tag(b, "LIST");           // unknown chunk
  put_u32(b, 3);                // odd size -> one pad byte follows
  b.push_back('a'); b.push_back('b'); b.push_back('c'); b.push_back(0);  // +pad
  put_tag(b, "data");
  put_u32(b, static_cast<uint32_t>(data.size()));
  b.insert(b.end(), data.begin(), data.end());
  auto path = write_bytes("modsynth_list.wav", b);

  WavData w;
  REQUIRE(read_wav_f32(path.string(), w));
  std::filesystem::remove(path);
  REQUIRE(w.samples.size() == 1);
  CHECK(w.samples[0] == doctest::Approx(0.5f).epsilon(0.001));
}

TEST_CASE("read_wav_f32: rejects missing and malformed files") {
  WavData w;
  CHECK_FALSE(read_wav_f32("/nonexistent/path/nope.wav", w));

  auto junk = write_bytes("modsynth_junk.wav", {'n', 'o', 't', 'a', 'w', 'a', 'v'});
  CHECK_FALSE(read_wav_f32(junk.string(), w));
  std::filesystem::remove(junk);
}
