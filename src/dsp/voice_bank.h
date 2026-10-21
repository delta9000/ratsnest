#pragma once
// The voice-bank output convention shared by bank producers (`alloc`,
// `chord`): four 8-wide families, gate1..8, voct1..8, vel1..8, spread1..8,
// in that order. graph/voices.cpp keys voice groups on these names
// (docs/superpowers/specs/2026-07-11-voice-alloc-design.md §2-3).
#include "dsp/module.h"

namespace ms::dsp {

inline constexpr OutputInfo kVoiceBankOutputs[] = {
    {"gate1"}, {"gate2"}, {"gate3"}, {"gate4"},
    {"gate5"}, {"gate6"}, {"gate7"}, {"gate8"},
    {"voct1"}, {"voct2"}, {"voct3"}, {"voct4"},
    {"voct5"}, {"voct6"}, {"voct7"}, {"voct8"},
    {"vel1"}, {"vel2"}, {"vel3"}, {"vel4"},
    {"vel5"}, {"vel6"}, {"vel7"}, {"vel8"},
    {"spread1"}, {"spread2"}, {"spread3"}, {"spread4"},
    {"spread5"}, {"spread6"}, {"spread7"}, {"spread8"}};

// Stereo position of voice i of n, spread evenly over -1..1 (0 when n = 1).
inline float voice_spread(int i, int n) {
  if (n == 1) return 0.f;
  return -1.f + 2.f * static_cast<float>(i) / static_cast<float>(n - 1);
}

}  // namespace ms::dsp
