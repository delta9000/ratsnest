#include "io/wav.h"
#include <cstdint>
#include <cstdio>
#include <cstring>

namespace ms::io {
namespace {
void put_u32(std::FILE* f, uint32_t v) { std::fwrite(&v, 4, 1, f); }
void put_u16(std::FILE* f, uint16_t v) { std::fwrite(&v, 2, 1, f); }

uint32_t rd_u32(const uint8_t* p) {
  return static_cast<uint32_t>(p[0]) | (static_cast<uint32_t>(p[1]) << 8) |
         (static_cast<uint32_t>(p[2]) << 16) | (static_cast<uint32_t>(p[3]) << 24);
}
uint16_t rd_u16(const uint8_t* p) {
  return static_cast<uint16_t>(static_cast<uint32_t>(p[0]) |
                               (static_cast<uint32_t>(p[1]) << 8));
}
bool tag_eq(const uint8_t* p, const char* t) {
  return p[0] == static_cast<uint8_t>(t[0]) && p[1] == static_cast<uint8_t>(t[1]) &&
         p[2] == static_cast<uint8_t>(t[2]) && p[3] == static_cast<uint8_t>(t[3]);
}
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

bool read_wav_f32(const std::string& path, WavData& out) {
  out = WavData{};
  std::FILE* f = std::fopen(path.c_str(), "rb");
  if (!f) return false;
  std::fseek(f, 0, SEEK_END);
  const long sz = std::ftell(f);
  std::fseek(f, 0, SEEK_SET);
  if (sz < 44) { std::fclose(f); return false; }
  std::vector<uint8_t> buf(static_cast<size_t>(sz));
  const size_t got = std::fread(buf.data(), 1, buf.size(), f);
  std::fclose(f);
  if (got != buf.size()) return false;

  if (!tag_eq(&buf[0], "RIFF") || !tag_eq(&buf[8], "WAVE")) return false;

  // Walk chunks from offset 12, honoring each chunk's declared size and the
  // RIFF word-alignment pad after odd-sized chunk bodies.
  uint16_t fmt = 0, channels = 0, bits = 0;
  uint32_t rate = 0;
  const uint8_t* data_ptr = nullptr;
  size_t data_len = 0;
  bool have_fmt = false;

  size_t pos = 12;
  while (pos + 8 <= buf.size()) {
    const uint8_t* chunk = &buf[pos];
    uint32_t csize = rd_u32(chunk + 4);
    const size_t body = pos + 8;
    if (body + csize > buf.size()) csize = static_cast<uint32_t>(buf.size() - body);

    if (tag_eq(chunk, "fmt ") && csize >= 16) {
      fmt = rd_u16(&buf[body]);
      channels = rd_u16(&buf[body + 2]);
      rate = rd_u32(&buf[body + 4]);
      bits = rd_u16(&buf[body + 14]);
      // WAVE_FORMAT_EXTENSIBLE: the real format code is the first two bytes of
      // the SubFormat GUID at offset 24 within the fmt chunk.
      if (fmt == 0xFFFE && csize >= 40) fmt = rd_u16(&buf[body + 24]);
      have_fmt = true;
    } else if (tag_eq(chunk, "data")) {
      data_ptr = &buf[body];
      data_len = csize;
    }
    pos = body + csize + (csize & 1u);
  }

  if (!have_fmt || data_ptr == nullptr || channels == 0 || rate == 0) return false;

  const size_t bytes_per = static_cast<size_t>(bits) / 8;
  if (bytes_per == 0) return false;
  const size_t total = data_len / bytes_per;  // samples across all channels

  std::vector<float> s;
  s.reserve(total);

  if (fmt == 3 && bits == 32) {
    for (size_t i = 0; i < total; ++i) {
      float v;
      std::memcpy(&v, data_ptr + i * 4, 4);
      s.push_back(v);
    }
  } else if (fmt == 1 && bits == 16) {
    for (size_t i = 0; i < total; ++i) {
      int16_t v;
      std::memcpy(&v, data_ptr + i * 2, 2);
      s.push_back(static_cast<float>(v) / 32768.0f);
    }
  } else if (fmt == 1 && bits == 24) {
    for (size_t i = 0; i < total; ++i) {
      const uint8_t* q = data_ptr + i * 3;
      uint32_t u = static_cast<uint32_t>(q[0]) | (static_cast<uint32_t>(q[1]) << 8) |
                   (static_cast<uint32_t>(q[2]) << 16);
      if (u & 0x800000u) u |= 0xFF000000u;  // sign-extend 24 -> 32
      s.push_back(static_cast<float>(static_cast<int32_t>(u)) / 8388608.0f);
    }
  } else if (fmt == 1 && bits == 32) {
    for (size_t i = 0; i < total; ++i) {
      int32_t v;
      std::memcpy(&v, data_ptr + i * 4, 4);
      s.push_back(static_cast<float>(v) / 2147483648.0f);
    }
  } else {
    return false;  // unsupported format/bit-depth
  }

  out.channels = channels;
  out.sample_rate = static_cast<int>(rate);
  out.frames = s.size() / static_cast<size_t>(channels);
  out.samples = std::move(s);
  return true;
}
}  // namespace ms::io
