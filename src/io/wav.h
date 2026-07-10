#pragma once
#include <cstddef>
#include <string>

namespace ms::io {
bool write_wav_f32(const std::string& path, const float* interleaved,
                   size_t frames, int channels, int sample_rate);
}  // namespace ms::io
