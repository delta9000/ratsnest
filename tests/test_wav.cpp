#include <doctest/doctest.h>
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <vector>
#include "io/wav.h"

TEST_CASE("writes a valid float32 stereo wav") {
  auto path = std::filesystem::temp_directory_path() / "ratsnest_test.wav";
  float data[8] = {0.f, 0.f, 0.5f, -0.5f, 1.f, -1.f, 0.25f, 0.25f};  // 4 frames
  REQUIRE(ms::io::write_wav_f32(path.string(), data, 4, 2, 48000));

  std::FILE* f = std::fopen(path.string().c_str(), "rb");
  REQUIRE(f != nullptr);
  std::vector<uint8_t> bytes(1024);
  size_t n = std::fread(bytes.data(), 1, bytes.size(), f);
  std::fclose(f);
  std::filesystem::remove(path);

  REQUIRE(n == 44 + 8 * 4);                       // header + 8 samples * 4 bytes
  CHECK(std::string(bytes.begin(), bytes.begin() + 4) == "RIFF");
  CHECK(std::string(bytes.begin() + 8, bytes.begin() + 12) == "WAVE");
  CHECK(bytes[20] == 3);                          // format tag: IEEE float
  CHECK(bytes[22] == 2);                          // channels
  uint32_t rate = static_cast<uint32_t>(bytes[24]) | (bytes[25] << 8u) |
                  (bytes[26] << 16u) | (static_cast<uint32_t>(bytes[27]) << 24u);
  CHECK(rate == 48000);
  float first_sample;
  std::memcpy(&first_sample, &bytes[44], 4);
  CHECK(first_sample == doctest::Approx(0.0f));
}

TEST_CASE("writes dithered 16- and 24-bit PCM that reads back within an LSB") {
  auto path = std::filesystem::temp_directory_path() / "ratsnest_test_pcm.wav";
  std::vector<float> data;
  for (int i = 0; i < 2000; ++i) data.push_back(0.5f * static_cast<float>(std::sin(i * 0.01)));
  for (int bits : {16, 24}) {
    REQUIRE(ms::io::write_wav_pcm(path.string(), data.data(), 1000, 2, 48000, bits));
    ms::io::WavData w;
    REQUIRE(ms::io::read_wav_f32(path.string(), w));
    CHECK(w.channels == 2);
    CHECK(w.frames == 1000);
    const double lsb = 1.0 / (bits == 16 ? 32768.0 : 8388608.0);
    double worst = 0.0, sum = 0.0;
    for (size_t i = 0; i < data.size(); ++i) {
      const double e = static_cast<double>(w.samples[i]) - data[i];
      worst = std::max(worst, std::abs(e));
      sum += e;
    }
    CHECK(worst <= 1.5 * lsb);                                        // TPDF: +-1 LSB, rounded
    CHECK(std::abs(sum / static_cast<double>(data.size())) < 0.1 * lsb);  // unbiased
  }
  // Same input, same bytes: the dither is reproducible.
  std::vector<float> zero(200, 0.0f);
  REQUIRE(ms::io::write_wav_pcm(path.string(), zero.data(), 100, 2, 48000, 16));
  ms::io::WavData a, b;
  REQUIRE(ms::io::read_wav_f32(path.string(), a));
  REQUIRE(ms::io::write_wav_pcm(path.string(), zero.data(), 100, 2, 48000, 16));
  REQUIRE(ms::io::read_wav_f32(path.string(), b));
  CHECK(a.samples == b.samples);
  CHECK_FALSE(ms::io::write_wav_pcm(path.string(), zero.data(), 100, 2, 48000, 12));
  std::filesystem::remove(path);
}
