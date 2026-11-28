#pragma once
// Standard MIDI File reader for `render --midi`: offline, never RT.
#include <string>
#include <vector>
#include "dsp/module.h"

namespace ms::io {

struct TimedMidi {
  double seconds = 0.0;  // from the start of the file
  dsp::MidiEvent e{};
};

// Reads a format 0 or 1 file with a ticks-per-quarter division into its
// channel-voice events, merged across tracks and sorted by time (ties keep
// file order), with the tempo map applied. Meta events other than tempo,
// and sysex, are skipped. On failure returns {} and sets err.
std::vector<TimedMidi> read_smf(const std::string& path, std::string& err);
// The same, from bytes already in memory (`name` is for messages).
std::vector<TimedMidi> parse_smf(const std::string& bytes, const std::string& name,
                                 std::string& err);

}  // namespace ms::io
