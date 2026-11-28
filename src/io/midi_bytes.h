#pragma once
// MIDI 1.0 byte streams -> one dsp::MidiEvent per channel-voice or system
// common/realtime message. Header-only and allocation-free: the PipeWire MIDI
// input calls it on the audio side (engine/midi_in.cpp), and the Standard
// MIDI File reader (io/smf.cpp) reuses the message lengths.
#include <cstddef>
#include <cstdint>
#include "dsp/module.h"

namespace ms::io {

// Message length in bytes given a status byte, or 0 if unsupported (sysex
// and friends: dsp::MidiEvent carries only 1..3-byte messages).
inline size_t midi_message_length(uint8_t status) {
  switch (status & 0xf0) {
    case 0x80: case 0x90: case 0xa0: case 0xb0: case 0xe0: return 3;
    case 0xc0: case 0xd0: return 2;
    default: break;
  }
  switch (status) {
    case 0xf1: case 0xf3: return 2;
    case 0xf2: return 3;
    case 0xf6: return 1;
    default:
      if (status >= 0xf8) return 1;  // system realtime: single byte
      return 0;                      // sysex (0xf0) and anything else
  }
}

// Splits one buffer of raw MIDI into events, expanding running status (the
// status in effect resets at the start of each buffer). A stray data byte
// with no status is skipped; an unsupported message (sysex) or an incomplete
// tail drops the rest of the buffer. emit(const dsp::MidiEvent&) per event.
template <class Emit>
void parse_midi_bytes(const uint8_t* data, size_t len, Emit&& emit) {
  size_t i = 0;
  uint8_t running = 0;
  while (i < len) {
    const uint8_t b0 = data[i];
    uint8_t status;
    size_t data_start;
    if ((b0 & 0x80) != 0) {
      status = b0;
      data_start = i + 1;
      // System common clears running status; realtime (clock, active
      // sensing) may sit between messages and leaves it alone.
      if (status < 0xf8) running = status < 0xf0 ? status : 0;
    } else {
      status = running;
      data_start = i;
      if (status == 0) { ++i; continue; }  // stray data byte
    }
    const size_t msg_len = midi_message_length(status);
    if (msg_len == 0) return;                           // sysex etc.
    if (data_start + (msg_len - 1) > len) return;       // incomplete tail
    dsp::MidiEvent e{};
    e.data[0] = status;
    for (size_t k = 1; k < msg_len; ++k) e.data[k] = data[data_start + k - 1];
    e.len = static_cast<uint8_t>(msg_len);
    emit(e);
    i = data_start + (msg_len - 1);
  }
}

}  // namespace ms::io
