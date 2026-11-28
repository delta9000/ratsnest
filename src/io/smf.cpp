#include "io/smf.h"

#include <algorithm>
#include <cstdint>
#include <fstream>
#include <iterator>
#include <map>
#include <sstream>
#include "io/midi_bytes.h"

namespace ms::io {
namespace {

struct Reader {
  const std::string& b;
  size_t pos = 0;
  size_t end = 0;
  bool ok = true;
  uint8_t u8() {
    if (pos >= end) { ok = false; return 0; }
    return static_cast<uint8_t>(b[pos++]);
  }
  uint32_t be(int n) {
    uint32_t v = 0;
    for (int i = 0; i < n; ++i) v = (v << 8) | u8();
    return v;
  }
  uint32_t vlq() {  // variable-length quantity, at most 4 bytes
    uint32_t v = 0;
    for (int i = 0; i < 4; ++i) {
      const uint8_t c = u8();
      v = (v << 7) | (c & 0x7f);
      if (!(c & 0x80)) return v;
    }
    ok = false;
    return 0;
  }
};

struct Tick {
  uint64_t tick;
  size_t order;
  dsp::MidiEvent e;
};

// One MTrk body: channel events into `out`, tempo changes into `tempo`.
bool read_track(Reader& r, std::vector<Tick>& out, std::map<uint64_t, uint32_t>& tempo) {
  uint64_t tick = 0;
  uint8_t running = 0;
  while (r.ok && r.pos < r.end) {
    tick += r.vlq();
    uint8_t status = r.u8();
    if (status == 0xff) {  // meta
      const uint8_t type = r.u8();
      const uint32_t len = r.vlq();
      if (type == 0x51 && len == 3) tempo[tick] = r.be(3);
      else r.pos += len;
      if (type == 0x2f) return r.ok;  // end of track
      continue;
    }
    if (status == 0xf0 || status == 0xf7) {  // sysex: skip, cancels running status
      r.pos += r.vlq();
      running = 0;
      continue;
    }
    if (!(status & 0x80)) {  // running status: this byte is data
      if (running == 0) return false;
      --r.pos;
      status = running;
    } else {
      running = status;
    }
    const size_t len = midi_message_length(status);
    if (len < 2 || status >= 0xf0) return false;  // only channel messages in tracks
    Tick t{tick, out.size(), {}};
    t.e.data[0] = status;
    for (size_t k = 1; k < len; ++k) t.e.data[k] = static_cast<uint8_t>(r.u8() & 0x7f);
    t.e.len = static_cast<uint8_t>(len);
    out.push_back(t);
  }
  return r.ok;
}

}  // namespace

std::vector<TimedMidi> parse_smf(const std::string& bytes, const std::string& name,
                                 std::string& err) {
  Reader r{bytes, 0, bytes.size()};
  if (bytes.compare(0, 4, "MThd") != 0) { err = name + ": not a Standard MIDI File"; return {}; }
  r.pos = 4;
  const uint32_t hlen = r.be(4);
  const size_t body = r.pos;
  const uint32_t format = r.be(2), ntrks = r.be(2), division = r.be(2);
  r.pos = body + hlen;
  if (!r.ok || hlen < 6) { err = name + ": truncated header"; return {}; }
  if (format > 1) { err = name + ": format 2 (independent sequences) is not supported"; return {}; }
  if (division & 0x8000 || division == 0) { err = name + ": SMPTE time division is not supported"; return {}; }

  std::vector<Tick> ticks;
  std::map<uint64_t, uint32_t> tempo;  // tick -> microseconds per quarter
  for (uint32_t t = 0; t < ntrks && r.ok; ++t) {
    if (r.pos + 8 > bytes.size()) break;
    const std::string id = bytes.substr(r.pos, 4);
    r.pos += 4;
    const uint32_t len = r.be(4);
    const size_t next = r.pos + len;
    if (id != "MTrk") { r.pos = next; continue; }  // unknown chunks are skipped
    Reader tr{bytes, r.pos, std::min(next, bytes.size())};
    if (!read_track(tr, ticks, tempo)) {
      err = name + ": malformed track " + std::to_string(t + 1) + " near byte " + std::to_string(tr.pos);
      return {};
    }
    r.pos = next;
  }
  std::stable_sort(ticks.begin(), ticks.end(),
                   [](const Tick& a, const Tick& b) { return a.tick < b.tick; });

  // Ticks -> seconds through the tempo map (120 bpm until the first change).
  std::vector<TimedMidi> out;
  out.reserve(ticks.size());
  double seconds = 0.0;
  uint64_t at = 0;
  uint32_t us = 500000;
  auto next_tempo = tempo.begin();
  for (const Tick& t : ticks) {
    while (next_tempo != tempo.end() && next_tempo->first <= t.tick) {
      seconds += static_cast<double>(next_tempo->first - at) * us * 1e-6 / division;
      at = next_tempo->first;
      us = next_tempo->second;
      ++next_tempo;
    }
    out.push_back({seconds + static_cast<double>(t.tick - at) * us * 1e-6 / division, t.e});
  }
  return out;
}

std::vector<TimedMidi> read_smf(const std::string& path, std::string& err) {
  std::ifstream f(path, std::ios::binary);
  if (!f) { err = "cannot read '" + path + "'"; return {}; }
  const std::string bytes((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
  return parse_smf(bytes, path, err);
}

}  // namespace ms::io
