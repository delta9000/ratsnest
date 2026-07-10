#include "io/wav.h"
#include <cstdint>
#include <cstdio>

namespace ms::io {
namespace {
void put_u32(std::FILE* f, uint32_t v) { std::fwrite(&v, 4, 1, f); }
void put_u16(std::FILE* f, uint16_t v) { std::fwrite(&v, 2, 1, f); }
}  // namespace

bool write_wav_f32(const std::string& path, const float* interleaved,
                   size_t frames, int channels, int sample_rate) {
  std::FILE* f = std::fopen(path.c_str(), "wb");
  if (!f) return false;
  const uint32_t data_bytes = static_cast<uint32_t>(frames * static_cast<size_t>(channels) * 4);
  const uint16_t ch = static_cast<uint16_t>(channels);
  const uint32_t byte_rate = static_cast<uint32_t>(sample_rate) * ch * 4;

  std::fwrite("RIFF", 1, 4, f);
  put_u32(f, 36 + data_bytes);
  std::fwrite("WAVE", 1, 4, f);
  std::fwrite("fmt ", 1, 4, f);
  put_u32(f, 16);
  put_u16(f, 3);  // IEEE float
  put_u16(f, ch);
  put_u32(f, static_cast<uint32_t>(sample_rate));
  put_u32(f, byte_rate);
  put_u16(f, static_cast<uint16_t>(ch * 4));
  put_u16(f, 32);
  std::fwrite("data", 1, 4, f);
  put_u32(f, data_bytes);
  const size_t written = std::fwrite(interleaved, 4, frames * static_cast<size_t>(channels), f);
  std::fclose(f);
  return written == frames * static_cast<size_t>(channels);
}
}  // namespace ms::io
