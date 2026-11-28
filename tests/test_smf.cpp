// MIDI byte parsing (the live PipeWire input's normalization point) and the
// Standard MIDI File reader behind `render --midi`.
#include <doctest/doctest.h>
#include <cstdint>
#include <random>
#include <string>
#include <vector>
#include "io/midi_bytes.h"
#include "io/smf.h"

using namespace ms;

namespace {

std::vector<dsp::MidiEvent> parse(std::vector<uint8_t> b) {
  std::vector<dsp::MidiEvent> out;
  io::parse_midi_bytes(b.data(), b.size(), [&](const dsp::MidiEvent& e) { out.push_back(e); });
  return out;
}

std::string vlq(uint32_t n) {
  std::string b(1, static_cast<char>(n & 0x7f));
  while (n >>= 7) b.insert(b.begin(), static_cast<char>((n & 0x7f) | 0x80));
  return b;
}
std::string be(uint32_t v, int n) {
  std::string b;
  for (int i = n - 1; i >= 0; --i) b += static_cast<char>((v >> (8 * i)) & 0xff);
  return b;
}
std::string bytes(std::initializer_list<int> v) {
  std::string b;
  for (int x : v) b += static_cast<char>(x);
  return b;
}
std::string track(const std::string& body) {
  const std::string d = body + vlq(0) + bytes({0xff, 0x2f, 0x00});
  return "MTrk" + be(static_cast<uint32_t>(d.size()), 4) + d;
}
std::string header(int format, int tracks, int division) {
  return "MThd" + be(6, 4) + be(static_cast<uint32_t>(format), 2) +
         be(static_cast<uint32_t>(tracks), 2) + be(static_cast<uint32_t>(division), 2);
}

}  // namespace

TEST_CASE("midi bytes: running status, realtime between messages, and drops") {
  auto e = parse({0x90, 60, 100, 62, 100, 0xf8, 64, 0});  // realtime keeps running status
  REQUIRE(e.size() == 4);
  CHECK(e[1].data[0] == 0x90);
  CHECK(e[1].data[1] == 62);
  CHECK(e[2].len == 1);
  CHECK(e[2].data[0] == 0xf8);
  CHECK(e[3].data[0] == 0x90);
  CHECK(e[3].data[1] == 64);
  CHECK(e[3].data[2] == 0);

  CHECK(parse({60, 100}).empty());                  // stray data, no status
  CHECK(parse({0x90, 60}).empty());                 // incomplete tail dropped
  CHECK(parse({0xc0, 5, 7}).size() == 2);           // 2-byte program change, running
  auto s = parse({0x90, 60, 1, 0xf0, 1, 2, 0xf7});  // sysex drops the rest
  CHECK(s.size() == 1);
  auto c = parse({0x90, 60, 1, 0xf2, 1, 2, 62, 1});  // system common clears running status
  REQUIRE(c.size() == 2);
  CHECK(c[1].data[0] == 0xf2);
}

TEST_CASE("smf: two tracks merge in time through a tempo change") {
  // Track 1: tempo 120 bpm, then 240 bpm at beat 2. Track 2: notes on
  // beats 0, 1, 2, 3 (running status), one after a sysex.
  const std::string t1 = vlq(0) + bytes({0xff, 0x51, 0x03}) + be(500000, 3) + vlq(192) +
                         bytes({0xff, 0x51, 0x03}) + be(250000, 3);
  const std::string t2 = vlq(0) + bytes({0x90, 60, 100}) + vlq(96) + bytes({62, 100}) +
                         vlq(0) + bytes({0xf0}) + vlq(2) + bytes({1, 0xf7}) +
                         vlq(96) + bytes({0x90, 64, 100}) + vlq(96) + bytes({65, 100});
  std::string err;
  const auto ev = io::parse_smf(header(1, 2, 96) + track(t1) + track(t2), "t", err);
  CHECK(err.empty());
  REQUIRE(ev.size() == 4);
  CHECK(ev[0].seconds == doctest::Approx(0.0));
  CHECK(ev[1].seconds == doctest::Approx(0.5));
  CHECK(ev[2].seconds == doctest::Approx(1.0));
  CHECK(ev[3].seconds == doctest::Approx(1.25));  // 240 bpm after beat 2
  CHECK(ev[1].e.data[1] == 62);
  CHECK(ev[3].e.data[0] == 0x90);
}

TEST_CASE("smf: unsupported or broken files report instead of crashing") {
  std::string err;
  CHECK(io::parse_smf("RIFF....", "x", err).empty());
  CHECK(err.find("not a Standard MIDI File") != std::string::npos);
  CHECK(io::parse_smf(header(2, 1, 96) + track(""), "x", err).empty());
  CHECK(err.find("format 2") != std::string::npos);
  CHECK(io::parse_smf(header(0, 1, 0xe728), "x", err).empty());
  CHECK(err.find("SMPTE") != std::string::npos);
  CHECK(io::parse_smf(header(0, 1, 96) + track(vlq(0) + bytes({60, 100})), "x", err).empty());
  CHECK(err.find("malformed track 1") != std::string::npos);

  // Mutation fuzz: flipped and truncated bytes never crash or hang.
  const std::string good = header(0, 1, 96) + track(vlq(0) + bytes({0x90, 60, 100}) + vlq(96) +
                                                    bytes({0x80, 60, 0}));
  std::mt19937 rng(7);
  for (int i = 0; i < 2000; ++i) {
    std::string m = good;
    for (int k = 0; k < 3; ++k) m[rng() % m.size()] = static_cast<char>(rng() & 0xff);
    m.resize(rng() % (m.size() + 1));
    err.clear();
    (void)io::parse_smf(m, "fuzz", err);
  }
  CHECK(io::read_smf("/nonexistent/file.mid", err).empty());
  CHECK(err.find("cannot read") != std::string::npos);
}
