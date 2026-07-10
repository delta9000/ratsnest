#include <doctest/doctest.h>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <vector>
#include "io/wav.h"

TEST_CASE("writes a valid float32 stereo wav") {
  auto path = std::filesystem::temp_directory_path() / "modsynth_test.wav";
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
