#pragma once
// What a literal's suffix says it measures, and what an input expects. A
// unit only marks the literal (hz, s and bpm scale by 1), so without a check
// `freq: 2s` would quietly mean 2 Hz. A bare number fits any input.
namespace ms {

enum class Unit : unsigned char {
  None,   // bare number, or an input that takes any
  Freq,   // hz, khz, a note name (A4)
  Time,   // s, ms
  Tempo,  // bpm
  Pitch,  // v/oct note name (A4v)
};

constexpr const char* unit_name(Unit u) {
  switch (u) {
    case Unit::Freq:  return "a frequency (hz, khz, or a note like A4)";
    case Unit::Time:  return "a time (s, ms)";
    case Unit::Tempo: return "a tempo (bpm)";
    case Unit::Pitch: return "a v/oct pitch (a note like A4v)";
    default:          return "a plain number";
  }
}

// True when a literal written in `lit` may set an input declared in `in`.
constexpr bool unit_fits(Unit lit, Unit in) {
  return lit == Unit::None || in == Unit::None || lit == in;
}

}  // namespace ms
