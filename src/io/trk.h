#pragma once
// The `.trk` song format read by the `tracker` module; see
// docs/superpowers/specs/2026-10-21-tracker-design.md. Parsing and flattening
// run off the audio thread; the result is plain arrays the module indexes.
#include <string_view>
#include <vector>
#include "dsl/diag.h"

namespace ms::io {

constexpr int kTrkTracks = 8;         // output banks voct1..8, gate1..8, ...
constexpr int kTrkMaxRows = 65536;    // whole flattened song
constexpr int kTrkMaxPatternRows = 1024;

// One track's resolved state on one row: the held values, whether a note
// sounds, and whether this row re-articulates it (new note or `x`).
struct TrkRow {
  float voct = 0.0f, vel = 1.0f, cva = 0.0f, cvb = 0.0f;
  bool on = false, retrig = false;
};

struct TrkSong {
  int tracks = 0;
  std::vector<TrkRow> rows;      // song row * tracks + track
  std::vector<int> step_start;   // first song row of each order step, + total
  std::vector<int> step_pattern; // file index of each step's first pattern
  int loop = -1;                 // step the song returns to; -1 = it ends
  int steps() const { return static_cast<int>(step_pattern.size()); }
  int length() const { return step_start.empty() ? 0 : step_start.back(); }
};

struct TrkParse {
  TrkSong song;
  Diags errors;  // the first error, with line:col; file left empty
  bool ok() const { return errors.empty(); }
};

TrkParse parse_trk(std::string_view text);

}  // namespace ms::io
