#pragma once
#include <cstddef>
#include <cstdio>
#include <string>
#include <vector>

namespace ms::io {
bool write_wav_f32(const std::string& path, const float* interleaved,
                   size_t frames, int channels, int sample_rate);

// Write a recorder take without replacing any earlier take. On success, path
// contains the chosen name (take.wav, take.2.wav, ...).
bool write_wav_take(std::string& path, const float* samples, size_t frames, int rate);
bool write_wav_header(std::FILE* file, size_t frames, int channels, int rate);

// Decoded PCM, interleaved and normalized to ~[-1, 1] as 32-bit float.
struct WavData {
  std::vector<float> samples;  // interleaved: frame 0 ch 0, frame 0 ch 1, ...
  int channels = 0;
  int sample_rate = 0;
  size_t frames = 0;           // samples.size() / channels
};

// Reads a canonical RIFF/WAVE file into `out`. Supports PCM integer (16/24/32
// bit) and IEEE float (32 bit), including WAVE_FORMAT_EXTENSIBLE. Returns false
// (and leaves `out` empty) on open failure, a malformed/short header, an
// unsupported format, or truncated data. Non-RT: opens and reads a file.
bool read_wav_f32(const std::string& path, WavData& out);
// Read a sample source, reporting a missing, invalid or empty WAV on stderr.
bool load_sample(const std::string& path, WavData& out);
}  // namespace ms::io
