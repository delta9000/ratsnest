#pragma once
// The voice-bank output convention shared by bank producers (`alloc`,
// `chord`): four kBankWidth-wide families, gate1..16, voct1..16, vel1..16, spread1..16,
// in that order. graph/voices.cpp keys voice groups on these names
// (docs/superpowers/specs/2026-07-11-voice-alloc-design.md §2-3).
#include "dsp/module.h"

namespace ms::dsp {

// Voices per bank: the widest voice group a bank producer can feed.
inline constexpr int kBankWidth = 16;

// stem1..stem16 as OutputInfo entries.
#define MS_BANK_FAMILY(stem)                                                      \
  {#stem "1"}, {#stem "2"}, {#stem "3"}, {#stem "4"}, {#stem "5"}, {#stem "6"},   \
      {#stem "7"}, {#stem "8"}, {#stem "9"}, {#stem "10"}, {#stem "11"},          \
      {#stem "12"}, {#stem "13"}, {#stem "14"}, {#stem "15"}, {#stem "16"}
inline constexpr OutputInfo kVoiceBankOutputs[] = {
    MS_BANK_FAMILY(gate), MS_BANK_FAMILY(voct), MS_BANK_FAMILY(vel), MS_BANK_FAMILY(spread)};

// Stereo position of voice i of n, spread evenly over -1..1 (0 when n = 1).
inline float voice_spread(int i, int n) {
  if (n == 1) return 0.f;
  return -1.f + 2.f * static_cast<float>(i) / static_cast<float>(n - 1);
}

}  // namespace ms::dsp
