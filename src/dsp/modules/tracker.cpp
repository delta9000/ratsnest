#include <algorithm>
#include <array>
#include "dsp/modules.h"
#include "dsp/util.h"
#include "io/trk.h"

namespace ms::dsp {
namespace {
constexpr InputInfo kIns[] = {
    {"clock", 0.f, false}, {"reset", 0.f, false}, {"start", 0.f, false}};
// `file` is read by the loader (graph/trackfile.cpp), which embeds the song
// as `text`; init only parses text, so it does no I/O.
constexpr ConfigInfo kCfg[] = {{"file", nullptr, ConfigKind::Text},
                               {"text", nullptr, ConfigKind::Text}};
// Banks like alloc/midi (`voct1..8`, ...), grouped per track: track t's
// columns are outputs 5t..5t+4.
#define MS_TRACK(n) {"voct" #n}, {"gate" #n}, {"vel" #n}, {"cva" #n}, {"cvb" #n}
constexpr OutputInfo kOuts[] = {
    MS_TRACK(1), MS_TRACK(2), MS_TRACK(3), MS_TRACK(4),
    MS_TRACK(5), MS_TRACK(6), MS_TRACK(7), MS_TRACK(8),
    {"step"}, {"pattern"}, {"row"}, {"step_trig"}, {"row_trig"}, {"end"}};
#undef MS_TRACK
constexpr int kCols = 5;
constexpr int kSongOut = io::kTrkTracks * kCols;

// Plays a .trk song: one row per rising clock edge, everything precomputed.
// Position is (order step, absolute song row); pos_ = -1 waits for the first
// edge after load or reset.
class Tracker final : public Module {
 public:
  void init(double, const Config& cfg) override {
    io::TrkParse p = io::parse_trk(cfg_str(cfg, "text", ""));
    if (p.ok()) song_ = std::move(p.song);
  }
  void process(const float* in, float* out) override {
    const bool reset = reset_edge_(in[1]);
    const bool tick = clock_edge_(in[0]);
    if (song_.length() == 0) return std::fill(out, out + kSongOut + 6, 0.0f);
    if (reset) rewind(in[2]);
    bool step_trig = false, wrapped = false;
    const bool played = tick && !ended_ && advance(step_trig, wrapped);
    tracks(out, played);
    const int start = song_.step_start[static_cast<size_t>(step_)];
    out[kSongOut] = static_cast<float>(step_);
    out[kSongOut + 1] = static_cast<float>(song_.step_pattern[static_cast<size_t>(step_)]);
    out[kSongOut + 2] = static_cast<float>(pos_ < 0 ? 0 : pos_ - start);
    out[kSongOut + 3] = as_gate(step_trig);
    out[kSongOut + 4] = as_gate(played);
    out[kSongOut + 5] = as_gate(ended_ || wrapped);
  }
  // Keeps the order step and the row within it, clamped to this song.
  void copy_state_from(const Module& other) override {
    const auto& o = static_cast<const Tracker&>(other);
    clock_edge_ = o.clock_edge_;
    reset_edge_ = o.reset_edge_;
    gate_ = o.gate_;
    ended_ = o.ended_;
    const int steps = song_.steps();
    if (steps == 0) return;
    step_ = std::min(o.step_, steps - 1);
    pos_ = -1;
    if (o.pos_ < 0) return;
    const int row = o.step_ < steps ? o.pos_ - o.song_.step_start[static_cast<size_t>(o.step_)]
                                    : io::kTrkMaxRows;
    const int first = song_.step_start[static_cast<size_t>(step_)];
    pos_ = std::min(first + row, song_.step_start[static_cast<size_t>(step_) + 1] - 1);
  }

 private:
  void rewind(float start) {
    step_ = round_clamp(start, 0, song_.steps() - 1, 0);
    pos_ = -1;
    ended_ = false;
  }
  // One clock edge. False when the song has just ended.
  bool advance(bool& step_trig, bool& wrapped) {
    const auto at = [&](int s) { return song_.step_start[static_cast<size_t>(s)]; };
    if (pos_ >= 0 && ++pos_ < at(step_ + 1)) return true;
    step_trig = true;
    if (pos_ < 0) return (pos_ = at(step_)), true;
    if (++step_ < song_.steps()) return true;
    if (song_.loop < 0) {
      ended_ = true;
      --step_;
      --pos_;
      return false;
    }
    step_ = song_.loop;
    pos_ = at(step_);
    wrapped = true;
    return true;
  }
  // A re-articulated note drops its gate for the edge sample only.
  void tracks(float* out, bool played) {
    const int row = pos_ < 0 ? song_.step_start[static_cast<size_t>(step_)] : pos_;
    for (int t = 0; t < io::kTrkTracks; ++t) {
      float* o = out + t * kCols;
      if (t >= song_.tracks) {
        std::fill(o, o + kCols, 0.0f);
        continue;
      }
      const io::TrkRow& r = song_.rows[static_cast<size_t>(row * song_.tracks + t)];
      bool& g = gate_[static_cast<size_t>(t)];
      g = pos_ >= 0 && !ended_ && r.on && !(played && r.retrig && g);
      o[0] = r.voct;
      o[1] = as_gate(g);
      o[2] = r.vel;
      o[3] = r.cva;
      o[4] = r.cvb;
    }
  }

  io::TrkSong song_;
  int step_ = 0, pos_ = -1;
  bool ended_ = false;
  std::array<bool, io::kTrkTracks> gate_{};
  RisingEdge clock_edge_, reset_edge_;
};
std::unique_ptr<Module> make() { return std::make_unique<Tracker>(); }
}  // namespace
const ModuleInfo tracker_info{"tracker", kIns, kCfg, kOuts, &make};
}  // namespace ms::dsp
