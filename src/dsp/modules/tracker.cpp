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
                               {"text", nullptr, ConfigKind::Text},
                               {"seed", nullptr}};
// Banks like alloc/midi (`voct1..16`, ...), grouped per track: track t's
// columns are outputs 6t..6t+5. Patches cable by name, so the order may grow.
#define MS_TRACK(n) {"voct" #n}, {"gate" #n}, {"vel" #n}, {"cva" #n}, {"cvb" #n}, {"cvd" #n}
constexpr OutputInfo kOuts[] = {
    MS_TRACK(1),  MS_TRACK(2),  MS_TRACK(3),  MS_TRACK(4),
    MS_TRACK(5),  MS_TRACK(6),  MS_TRACK(7),  MS_TRACK(8),
    MS_TRACK(9),  MS_TRACK(10), MS_TRACK(11), MS_TRACK(12),
    MS_TRACK(13), MS_TRACK(14), MS_TRACK(15), MS_TRACK(16),
    {"step"}, {"pattern"}, {"row"}, {"step_trig"}, {"row_trig"}, {"end"}};
#undef MS_TRACK
constexpr int kCols = 6;
static_assert(std::size(kOuts) == io::kTrkTracks * kCols + 6);
constexpr int kSongOut = io::kTrkTracks * kCols;

// One track's events inside the current row: the row whose change is
// pending (`row`, -1 none), samples until the next hit (`wait`, -1 none),
// hits after that one and the samples between them.
struct Pending { int row = -1, wait = -1, hits = 0, gap = 0; };

// Plays a .trk song: one row per rising clock edge, everything precomputed.
// Position is (order step, absolute song row); pos_ = -1 waits for the first
// edge after load or reset. Delays and ratchets are fractions of a measured
// row length, so they follow tempo without a tempo input.
class Tracker final : public Module {
 public:
  void init(double, const Config& cfg) override {
    io::TrkParse p = io::parse_trk(cfg_str(cfg, "text", ""));
    if (p.ok()) song_ = std::move(p.song);
    seed_ = rng_ = cfg_seed(cfg);
  }
  void process(const float* in, float* out) override {
    const bool reset = reset_edge_(in[1]);
    const bool tick = clock_edge_(in[0]);
    measure(tick);
    if (song_.length() == 0) return std::fill(out, out + kSongOut + 6, 0.0f);
    if (reset) rewind(in[2]);
    bool step_trig = false, wrapped = false;
    const bool played = tick && !ended_ && advance(step_trig, wrapped);
    if (played) schedule();
    if (tick && ended_) settle();
    tracks(out);
    const int start = song_.step_start[static_cast<size_t>(step_)];
    out[kSongOut] = static_cast<float>(step_);
    out[kSongOut + 1] = static_cast<float>(song_.step_pattern[static_cast<size_t>(step_)]);
    out[kSongOut + 2] = static_cast<float>(pos_ < 0 ? 0 : pos_ - start);
    out[kSongOut + 3] = as_gate(step_trig);
    out[kSongOut + 4] = as_gate(played);
    out[kSongOut + 5] = as_gate(ended_ || wrapped);
  }
  // Keeps the order step and the row within it, clamped to this song, and
  // shows the edited row at once; events still pending in the row drop.
  void copy_state_from(const Module& other) override {
    const auto& o = static_cast<const Tracker&>(other);
    clock_edge_ = o.clock_edge_;
    reset_edge_ = o.reset_edge_;
    gate_ = o.gate_;
    mute_ = o.mute_;
    ended_ = o.ended_;
    since_ = o.since_;
    last_ = o.last_;
    period_ = o.period_;
    rng_ = o.rng_;
    const int steps = song_.steps();
    if (steps == 0) return;
    step_ = std::min(o.step_, steps - 1);
    pos_ = -1;
    if (o.pos_ < 0) return;
    const int row = o.step_ < steps ? o.pos_ - o.song_.step_start[static_cast<size_t>(o.step_)]
                                    : io::kTrkMaxRows;
    const int first = song_.step_start[static_cast<size_t>(step_)];
    pos_ = std::min(first + row, song_.step_start[static_cast<size_t>(step_) + 1] - 1);
    for (int t = 0; t < song_.tracks; ++t) cur_[static_cast<size_t>(t)] = at(pos_, t);
  }

 private:
  const io::TrkRow& at(int row, int t) const {
    return song_.rows[static_cast<size_t>(row * song_.tracks + t)];
  }
  // A row's length is taken as the one two rows back: under a transport's
  // swing rows alternate long and short, so that is the row's own length,
  // and on a straight grid it is the period. 0 until three edges are seen.
  void measure(bool tick) {
    if (since_ < kNever) ++since_;
    if (!tick) return;
    period_ = last_;
    last_ = since_ < kNever ? since_ : 0;
    since_ = 0;
  }
  void rewind(float start) {
    step_ = round_clamp(start, 0, song_.steps() - 1, 0);
    pos_ = -1;
    ended_ = false;
    rng_ = seed_;
    pend_.fill({});
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
  // Lands whatever the last row still owed, so the song ends on its values.
  void settle() {
    for (int t = 0; t < song_.tracks; ++t) {
      Pending& p = pend_[static_cast<size_t>(t)];
      if (p.row >= 0) cur_[static_cast<size_t>(t)] = at(p.row, t);
      p = {};
    }
  }
  // A new row: a change the last one still owed lands now, and this row's
  // hits are spread over the rest of the period (all at the edge until a
  // period has been measured).
  void schedule() {
    settle();
    const float span = static_cast<float>(period_);
    for (int t = 0; t < song_.tracks; ++t) {
      const io::TrkRow& r = at(pos_, t);
      const int hits = period_ > 0 ? r.ratchet : 1;
      const float rest = span * (1.0f - r.delay);
      pend_[static_cast<size_t>(t)] = {pos_, static_cast<int>(span * r.delay), hits - 1,
                                        std::max(1, static_cast<int>(rest / static_cast<float>(hits)))};
    }
  }
  // Track t's due hit, if any: true when it re-articulates the note. The
  // first hit lands the row's values and rolls its chance.
  bool hit(int t) {
    Pending& p = pend_[static_cast<size_t>(t)];
    if (p.wait != 0) {
      if (p.wait > 0) --p.wait;
      return false;
    }
    bool art = true;
    if (p.row >= 0) {
      const io::TrkRow& r = at(p.row, t);
      cur_[static_cast<size_t>(t)] = r;
      art = r.retrig;
      if (art) mute_[static_cast<size_t>(t)] = unit_float(xorshift32(rng_)) >= r.chance;
      p.row = -1;
    }
    p.wait = p.hits-- > 0 ? p.gap - 1 : -1;
    return art;
  }
  // A re-articulated note drops its gate for one sample.
  void tracks(float* out) {
    const int first = song_.step_start[static_cast<size_t>(step_)];
    for (int t = 0; t < io::kTrkTracks; ++t) {
      float* o = out + t * kCols;
      if (t >= song_.tracks) {
        std::fill(o, o + kCols, 0.0f);
        continue;
      }
      const size_t i = static_cast<size_t>(t);
      if (pos_ < 0) cur_[i] = at(first, t);
      const bool art = hit(t);
      const io::TrkRow& r = cur_[i];
      bool& g = gate_[i];
      g = pos_ >= 0 && !ended_ && r.on && !mute_[i] && !(art && g);
      o[0] = r.voct;
      o[1] = as_gate(g);
      o[2] = r.vel;
      o[3] = r.cva;
      o[4] = r.cvb;
      o[5] = r.cvd;
    }
  }

  io::TrkSong song_;
  static constexpr int kNever = 1 << 30;
  int step_ = 0, pos_ = -1, since_ = kNever, last_ = 0, period_ = 0;
  bool ended_ = false;
  std::uint32_t seed_ = 1, rng_ = 1;
  std::array<io::TrkRow, io::kTrkTracks> cur_{};
  std::array<Pending, io::kTrkTracks> pend_{};
  std::array<bool, io::kTrkTracks> gate_{}, mute_{};
  RisingEdge clock_edge_, reset_edge_;
};
std::unique_ptr<Module> make() { return std::make_unique<Tracker>(); }
}  // namespace
const ModuleInfo tracker_info{"tracker", kIns, kCfg, kOuts, &make};
}  // namespace ms::dsp
