// 6-operator phase-modulation voice after Chowning's published FM formula
// (concept-inspired; see docs/attribution.md). Topology, algorithm set,
// scaling, and envelope design are original to ratsnest — no Yamaha/DX data,
// layouts, or code are used anywhere in this file.
//
// Operator waveforms (rectified/windowed/folded sine) are the TX81Z/DX11/FS1R
// operator-waveform *concept*, implemented locally from the mathematics; no
// Yamaha tables/code and no GPL/emulator code are used. Keyboard level/rate
// scaling is a standard technique in an original octave-exponent form.
#include <algorithm>
#include <array>
#include <cmath>
#include <numbers>
#include <utility>
#include <vector>
#include "dsp/input_key.h"
#include "dsp/modules.h"
#include "dsp/util.h"

namespace ms::dsp {
namespace {

constexpr int kOps = 6;
constexpr float kTwoPi = 2.0f * std::numbers::pi_v<float>;
// Phase deviation carried by a level-1 modulator at envelope peak: ±4π rad
// (modulation index ≈ 12.6), the classic instruments' ballpark maximum.
constexpr float kIndex = 4.0f * std::numbers::pi_v<float>;

constexpr InputInfo kIns[] = {
    {"gate", 0.f},   {"freq", 220.f}, {"v_oct", 0.f},  {"vel", 1.f},
    {"lvl1", 0.f},   {"lvl2", 0.f},   {"lvl3", 0.f},   {"lvl4", 0.f},
    {"lvl5", 0.f},   {"lvl6", 0.f},   {"ratio1", 0.f}, {"ratio2", 0.f},
    {"ratio3", 0.f}, {"ratio4", 0.f}, {"ratio5", 0.f}, {"ratio6", 0.f},
    // Live broadcast: sums onto every op's `fb` config base. Named distinctly
    // from the `fb` config array so the DSL can tell them apart (input names
    // shadow config names at parse time; cf. levels/lvl, ratios/ratio).
    {"fbmod", 0.f}};
constexpr const char* kAlgos[] = {"free",  "triad", "dual", "cascade",
                                  "crown", "weave", "custom", nullptr};
constexpr const char* kCurves[] = {"lin", "exp", "log", nullptr};
constexpr const char* kSyncs[] = {"on", "off", nullptr};
constexpr const char* kWaves[] = {"sine", "half", "abs", "quarter", "fold", nullptr};
constexpr ConfigInfo kCfg[] = {
    {"algo", kAlgos},    {"route", nullptr, ConfigKind::Numbers}, {"ratios", nullptr, ConfigKind::Numbers},
    {"levels", nullptr, ConfigKind::Numbers}, {"fixed", nullptr, ConfigKind::Numbers}, {"fb", nullptr, ConfigKind::Numbers},
    {"vel_sens", nullptr, ConfigKind::Numbers},
    {"t1", nullptr, ConfigKind::Numbers},     {"t2", nullptr, ConfigKind::Numbers},    {"t3", nullptr, ConfigKind::Numbers},
    {"t4", nullptr, ConfigKind::Numbers},     {"l1", nullptr, ConfigKind::Numbers},    {"l2", nullptr, ConfigKind::Numbers},
    {"l3", nullptr, ConfigKind::Numbers},     {"curve", kCurves}, {"sync", kSyncs},
    {"waves", kWaves, ConfigKind::Enums},  {"key_break", nullptr},
    {"key_level", nullptr, ConfigKind::Numbers}, {"key_rate", nullptr, ConfigKind::Numbers}};
constexpr OutputInfo kOuts[] = {{"out"}, {"op1"}, {"op2"}, {"op3"},
                                {"op4"}, {"op5"}, {"op6"}};

struct OpEnv {
  enum class Stage { Idle, S1, S2, S3, Hold, Rel };
  Stage stage = Stage::Idle;
  double level = 0.0, start = 0.0, progress = 0.0;
};

struct EnvCfg {
  std::array<double, 4> t{};  // segment times, seconds
  std::array<double, 3> l{};  // S1..S3 target levels
};

// Operator waveform, θ in [0,1). Each is DC-centered (one-period mean
// subtracted) so rectified shapes add no DC to carriers and no constant phase
// to modulators. See docs/attribution.md for provenance.
float op_wave(int w, float th) {
  const float pi = std::numbers::pi_v<float>;
  switch (w) {
    case 1: return (th < 0.5f ? std::sin(kTwoPi * th) : 0.0f) - 1.0f / pi;  // half
    case 2: return std::fabs(std::sin(kTwoPi * th)) - 2.0f / pi;           // abs
    case 3: return (th < 0.25f ? std::sin(2.0f * kTwoPi * th) : 0.0f)      // quarter
                   - 1.0f / (2.0f * pi);
    case 4: {                                                              // fold
      const float x = 2.0f * std::sin(kTwoPi * th);            // drive 2x
      return (2.0f / pi) * std::asin(std::sin(0.5f * pi * x));  // triangle wavefold
    }
    default: return std::sin(kTwoPi * th);                                 // sine
  }
}
// Operator output for phase angle A (radians, unwrapped).
float wave_sample(int w, float A) {
  if (w == 0) return std::sin(A);  // bit-identical to the pre-waveform path
  float th = A * (0.5f / std::numbers::pi_v<float>);  // A / 2π, in cycles
  th -= std::floor(th);
  return op_wave(w, th);
}
void env_gate(OpEnv& e, bool rising, bool falling) {
  using S = OpEnv::Stage;
  if (rising) {
    e.start = e.level;
    e.progress = 0.0;
    e.stage = S::S1;
  } else if (falling && e.stage != S::Idle && e.stage != S::Rel) {
    e.start = e.level;
    e.progress = 0.0;
    e.stage = S::Rel;
  }
}
bool env_segment(OpEnv& e, double target, double seconds, int curve,
                 double rate) {
  e.progress = std::min(1.0, e.progress + env_increment(seconds, rate));
  e.level = e.start + (target - e.start) * env_curve(curve, e.progress);
  if (e.progress < 1.0) return false;
  e.level = target;
  e.start = target;
  e.progress = 0.0;
  return true;
}
void env_tick(OpEnv& e, const EnvCfg& c, int curve, double rate, double tscale) {
  using S = OpEnv::Stage;
  switch (e.stage) {
    case S::S1:
      if (env_segment(e, c.l[0], c.t[0] * tscale, curve, rate)) e.stage = S::S2;
      break;
    case S::S2:
      if (env_segment(e, c.l[1], c.t[1] * tscale, curve, rate)) e.stage = S::S3;
      break;
    case S::S3:
      if (env_segment(e, c.l[2], c.t[2] * tscale, curve, rate)) e.stage = S::Hold;
      break;
    case S::Hold:
      e.level = c.l[2];
      break;
    case S::Rel:
      if (env_segment(e, 0.0, c.t[3] * tscale, curve, rate)) {
        e.stage = S::Idle;
        e.level = 0.0;
      }
      break;
    case S::Idle:
      e.level = 0.0;
      break;
  }
}

using Edge = std::pair<int, int>;  // (src, dst), 0-based; src modulates dst

std::vector<Edge> named_edges(int algo) {
  switch (algo) {
    case 1: return {{1, 0}, {3, 2}, {5, 4}};                     // triad
    case 2: return {{2, 1}, {1, 0}, {5, 4}, {4, 3}};             // dual
    case 3: return {{5, 4}, {4, 3}, {3, 2}, {2, 1}, {1, 0}};     // cascade
    case 4: return {{1, 0}, {2, 0}, {3, 0}, {4, 0}, {5, 0}};     // crown
    case 5: return {{5, 3}, {4, 3}, {3, 1}, {2, 1}, {1, 0}};     // weave
    default: return {};                                          // free/custom
  }
}

// Flat (from,to) pairs, 1-based in the patch. Any malformation -> empty
// (free) edge set: the session never dies on a bad config array (rule 6).
std::vector<Edge> parse_route(const std::vector<double>& route) {
  std::vector<Edge> edges;
  if (route.size() % 2 != 0) return {};
  for (size_t i = 0; i < route.size(); i += 2) {
    const double a = route[i], b = route[i + 1];
    const bool ok = a == std::floor(a) && b == std::floor(b) && a >= 1.0 &&
                    a <= kOps && b >= 1.0 && b <= kOps && a != b;
    if (!ok) return {};
    const Edge e{static_cast<int>(a) - 1, static_cast<int>(b) - 1};
    if (std::find(edges.begin(), edges.end(), e) != edges.end()) return {};
    edges.push_back(e);
  }
  return edges;
}

std::string validate(const Config& cfg) {
  for (const auto& [name, value] : cfg) {
    if (name != "route" && (value.arr.size() > kOps || value.strs.size() > kOps))
      return "'" + name + "' accepts at most 6 operator entries";
  }
  const auto& route = cfg_arr(cfg, "route");
  if (route.empty()) return {};
  if (cfg_str(cfg, "algo", "free") != "custom")
    return "'route' requires algo: custom";
  const auto edges = parse_route(route);
  if (edges.size() * 2 != route.size())
    return "'route' expects distinct operator pairs 1..6, without self-edges (use fb)";
  std::array<int, kOps> indegree{};
  for (auto [s, d] : edges) { (void)s; ++indegree[static_cast<size_t>(d)]; }
  for (int count = 0; count < kOps; ++count) {
    auto it = std::find(indegree.begin(), indegree.end(), 0);
    if (it == indegree.end()) return "'route' contains a cycle; use fb for operator self-feedback";
    const int src = static_cast<int>(it - indegree.begin());
    *it = -1;
    for (auto [s, d] : edges)
      if (s == src) --indegree[static_cast<size_t>(d)];
  }
  return {};
}

class Fm final : public Module {
 public:
  void init(double rate, const Config& cfg) override {
    rate_ = rate;
    algo_ = cfg_enum(cfg, "algo", kAlgos);
    curve_ = cfg_enum(cfg, "curve", kCurves);
    sync_ = cfg_str(cfg, "sync", "on") != "off";

    fill(cfg, "ratios", ratios_, 1.0, 0.0, 1e9);
    fill(cfg, "levels", levels_, 0.0, 0.0, 1.0);
    if (cfg_arr(cfg, "levels").empty()) levels_[0] = 1.0;  // a lone carrier
    fill(cfg, "fixed", fixed_, 0.0, 0.0, 1e9);
    fill(cfg, "fb", fb_, 0.0, 0.0, 0.999);
    fill(cfg, "vel_sens", vel_sens_, 0.0, 0.0, 1.0);
    load_envelopes(cfg);
    const auto& wv = cfg_str_arr(cfg, "waves");
    for (size_t k = 0; k < kOps; ++k)  // sine is also the unknown-name fallback
      waves_[k] = k < wv.size() ? enum_index(kWaves, wv[k]) : 0;

    key_break_ = cfg_num(cfg, "key_break", 0.0);
    fill(cfg, "key_level", key_level_, 0.0, -8.0, 8.0);
    fill(cfg, "key_rate", key_rate_, 0.0, -8.0, 8.0);
    level_scale_.fill(1.0);
    time_scale_.fill(1.0);

    edges_ = algo_ == 6 ? parse_route(cfg_arr(cfg, "route")) : named_edges(algo_);
    build_order();

    for (auto& e : envs_) e = OpEnv{};
    phase_.fill(0.0);
    prev_post_.fill(0.0f);
    prev_gate_ = 0.0f;
  }

  void process(const float* ins, float* outs) override {
    const float gate = ins[0];
    const bool high = gate > 0.5f;
    const bool rising = high && prev_gate_ <= 0.5f;
    const bool falling = !high && prev_gate_ > 0.5f;
    prev_gate_ = gate;
    if (rising) note_on(ins[2]);
    for (auto& e : envs_) env_gate(e, rising, falling);

    const double base = base_freq(ins[1], ins[2]);
    const float vel = clamp_nan(ins[3], 0.0f, 1.0f);

    std::array<double, kOps> post{};
    for (const int op : order_) {
      const auto k = static_cast<size_t>(op);
      post[k] = operator_sample(k, ins, base, vel, post);
      outs[1 + k] = static_cast<float>(post[k]);
    }
    float out = 0.0f;
    for (size_t k = 0; k < kOps; ++k) {
      if (carrier_[k]) out += static_cast<float>(post[k]);
      prev_post_[k] = static_cast<float>(post[k]);
    }
    outs[0] = out;
  }

  void copy_state_from(const Module& other) override {
    const auto& o = static_cast<const Fm&>(other);
    envs_ = o.envs_;
    phase_ = o.phase_;
    prev_post_ = o.prev_post_;
    prev_gate_ = o.prev_gate_;
    level_scale_ = o.level_scale_;
    time_scale_ = o.time_scale_;
  }

 private:
  // Latch keyboard scaling from v_oct for the whole note (DX-style): a
  // mid-note pitch glide bends pitch without warping level or envelope.
  void note_on(float v_oct) {
    if (sync_) phase_.fill(0.0);
    const double d = static_cast<double>(clamp_nan(v_oct, -24.f, 24.f)) - key_break_;
    for (size_t k = 0; k < kOps; ++k) {
      level_scale_[k] = clamp_nan(std::exp2(key_level_[k] * d), 0.0, 16.0);
      time_scale_[k] = clamp_nan(std::exp2(-key_rate_[k] * d), 1e-3, 1e3);
    }
  }

  // Operator k's output this sample. `post` holds this sample's outputs of
  // the operators already run (every modulator of k, by build_order).
  double operator_sample(size_t k, const float* ins, double base, float vel,
                         const std::array<double, kOps>& post) {
    const double ratio =
        fixed_[k] > 0.0
            ? 0.0
            : static_cast<double>(clamp_nan(
                  static_cast<float>(ratios_[k]) + ins[10 + k], 0.0f, 1e9f));
    const double f = fixed_[k] > 0.0 ? std::min(fixed_[k], 0.45 * rate_) : base * ratio;
    phase_[k] += f / rate_;
    phase_[k] -= std::floor(phase_[k]);

    const double lvl =
        static_cast<double>(clamp_nan(static_cast<float>(levels_[k]) + ins[4 + k], 0.0f, 1.0f)) *
        (1.0 - vel_sens_[k] + vel_sens_[k] * static_cast<double>(vel)) * level_scale_[k];
    env_tick(envs_[k], env_cfg_[k], curve_, rate_, time_scale_[k]);
    const double fb_amt = static_cast<double>(
        clamp_nan(static_cast<float>(fb_[k]) + ins[16], 0.0f, 0.999f));
    double mod = fb_amt > 0.0 ? fb_amt * static_cast<double>(prev_post_[k]) : 0.0;
    for (int p : preds_[k]) mod += post[static_cast<size_t>(p)];
    const float A = kTwoPi * static_cast<float>(phase_[k]) + kIndex * static_cast<float>(mod);
    return envs_[k].level * lvl * static_cast<double>(wave_sample(waves_[k], A));
  }

 private:
  // t1..t4 segment times and l1..l3 levels, one entry per operator.
  void load_envelopes(const Config& cfg) {
    static const double kTDef[4] = {0.002, 0.1, 0.4, 0.2};
    static const double kLDef[3] = {1.0, 0.75, 0.5};
    static const char* const kTNames[4] = {"t1", "t2", "t3", "t4"};
    static const char* const kLNames[3] = {"l1", "l2", "l3"};
    std::array<double, kOps> v{};
    for (size_t s = 0; s < 4; ++s) {
      fill(cfg, kTNames[s], v, kTDef[s], 0.0, HUGE_VAL);
      for (size_t k = 0; k < kOps; ++k) env_cfg_[k].t[s] = v[k];
    }
    for (size_t s = 0; s < 3; ++s) {
      fill(cfg, kLNames[s], v, kLDef[s], 0.0, 1.0);
      for (size_t k = 0; k < kOps; ++k) env_cfg_[k].l[s] = v[k];
    }
  }

  static void fill(const Config& cfg, const char* name,
                   std::array<double, kOps>& dst, double def, double lo,
                   double hi) {
    const auto& a = cfg_arr(cfg, name);
    for (int k = 0; k < kOps; ++k)
      dst[static_cast<size_t>(k)] =
          k < static_cast<int>(a.size())
              ? clamp_nan(a[static_cast<size_t>(k)], lo, hi)
              : def;
  }

  // Kahn: modulators before their targets. A cycle falls back to free.
  void build_order() {
    std::array<int, kOps> indeg{};
    for (auto [s, d] : edges_) ++indeg[static_cast<size_t>(d)];
    std::array<bool, kOps> placed{};
    order_.fill(-1);
    for (int pos = 0; pos < kOps; ++pos) {
      int pick = -1;
      for (int k = 0; k < kOps; ++k)
        if (!placed[static_cast<size_t>(k)] && indeg[static_cast<size_t>(k)] == 0) {
          pick = k;
          break;
        }
      if (pick < 0) {  // cycle
        edges_.clear();
        for (int k = 0; k < kOps; ++k) order_[static_cast<size_t>(k)] = k;
        break;
      }
      order_[static_cast<size_t>(pos)] = pick;
      placed[static_cast<size_t>(pick)] = true;
      for (auto [s, d] : edges_)
        if (s == pick) --indeg[static_cast<size_t>(d)];
    }
    for (auto& p : preds_) p.clear();
    std::array<bool, kOps> has_out{};
    for (auto [s, d] : edges_) {
      preds_[static_cast<size_t>(d)].push_back(s);
      has_out[static_cast<size_t>(s)] = true;
    }
    for (int k = 0; k < kOps; ++k)
      carrier_[static_cast<size_t>(k)] = !has_out[static_cast<size_t>(k)];
  }

  // freq * 2^v_oct, memoized while both hold still (skips exp2 per sample).
  double base_freq(float freq, float v_oct) {
    if (pitch_key_.changed({freq, v_oct}))
      base_ = static_cast<double>(clamp_nan(freq, 0.0f, 0.45f * static_cast<float>(rate_))) *
              std::exp2(static_cast<double>(clamp_nan(v_oct, -24.f, 24.f)));
    return base_;
  }

  double rate_ = 48000.0;
  InputKey<2> pitch_key_;  // freq, v_oct -> base_
  double base_ = 0.0;
  int algo_ = 0;
  int curve_ = 0;
  bool sync_ = true;
  std::array<double, kOps> ratios_{}, levels_{}, fixed_{}, fb_{}, vel_sens_{};
  std::array<int, kOps> waves_{};
  double key_break_ = 0.0;
  std::array<double, kOps> key_level_{}, key_rate_{};
  std::array<double, kOps> level_scale_{}, time_scale_{};  // latched per note (migrated)
  std::array<EnvCfg, kOps> env_cfg_{};
  std::array<int, kOps> order_{};
  std::array<bool, kOps> carrier_{};
  std::vector<Edge> edges_;
  std::array<std::vector<int>, kOps> preds_;
  // runtime state (migrated on hot reload)
  std::array<OpEnv, kOps> envs_{};
  std::array<double, kOps> phase_{};
  std::array<float, kOps> prev_post_{};
  float prev_gate_ = 0.0f;
};

std::unique_ptr<Module> make() { return std::make_unique<Fm>(); }
}  // namespace

const ModuleInfo fm_info{"fm", kIns, kCfg, kOuts, &make, false, false, &validate};
}  // namespace ms::dsp
