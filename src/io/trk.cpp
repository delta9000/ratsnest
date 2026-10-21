#include "io/trk.h"
// Layout conventions borrowed from tracker text formats (concept only, see
// docs/attribution.md): `|`-prefixed cells from the OpenMPT/ModPlug pattern
// clipboard with dotted empty fields, `---`/`===` (tracker folklore, source
// unresolved), and the order list with a restart position.
#include <algorithm>
#include <cctype>
#include <charconv>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <string>
#include "dsl/lexer.h"
#include "dsl/suggest.h"

namespace ms::io {
namespace {

enum Field { kNote, kVel, kCva, kCvb };
constexpr const char* kFields[] = {"note", "vel", "cva", "cvb"};
enum class Kind : std::uint8_t { None, Note, Slur, Off, Hit };

struct Tok { std::string_view s; int col; };
struct Track { std::string name; std::vector<int> fields; };
// One cell as written: a note event plus the value fields it sets (bit f of
// `set` for v[f]); bit f of `pitch` marks a cv value written as a note name,
// which an order-list `^N` transposes along with the note.
struct Event {
  Kind kind = Kind::None;
  std::uint8_t set = 0, pitch = 0;
  float v[4] = {};
};
struct Pattern {
  std::string name;
  int rows = 0;
  std::vector<int> tracks;
  std::vector<Event> ev;  // row * tracks.size() + column
};
// A layer as written in the order list (`name^N`), then resolved.
struct Spec { Tok name; int shift = 0; };
struct Layer { int pat = 0; int shift = 0; };
struct Entry { std::vector<Spec> specs; int repeat = 1; std::vector<Layer> layers; };

bool is_name(std::string_view s) {
  constexpr std::string_view kLower = "abcdefghijklmnopqrstuvwxyz";
  return !s.empty() && kLower.find(s[0]) != std::string_view::npos &&
         s.find_first_not_of("abcdefghijklmnopqrstuvwxyz0123456789_") == std::string_view::npos;
}

// Index of the item named `n` in `items`, or -1.
template <class Items>
int find_named(const Items& items, std::string_view n) {
  const auto it = std::find_if(items.begin(), items.end(), [&](const auto& x) { return x.name == n; });
  return it == items.end() ? -1 : static_cast<int>(it - items.begin());
}

// " (did you mean 'x'?)" among the names of `items`.
template <class Items>
std::string hint(const Items& items, std::string_view n) {
  std::vector<std::string> names;
  for (const auto& x : items) names.push_back(x.name);
  return dsl::did_you_mean(n, names);
}

bool to_int(std::string_view s, int lo, int hi, int& out) {
  const auto r = std::from_chars(s.data(), s.data() + s.size(), out);
  return r.ec == std::errc{} && r.ptr == s.data() + s.size() && out >= lo && out <= hi;
}

// A run of only '.' or only '-': an empty field.
bool is_empty(std::string_view s) {
  return s.find_first_not_of('.') == std::string_view::npos ||
         s.find_first_not_of('-') == std::string_view::npos;
}

// A note name (always v/oct here) or a finite number within +/-1e6.
bool to_value(std::string_view s, float& out) {
  double d = 0.0;
  if (!s.empty() && s[0] >= 'A' && s[0] <= 'G') {
    const std::string v = s.back() == 'v' ? std::string(s) : std::string(s) + "v";
    if (!dsl::note_num(v, d)) return false;
  } else {
    const std::string str(s);
    char* end = nullptr;
    d = std::strtod(str.c_str(), &end);
    if (str.empty() || end != str.c_str() + str.size() || !std::isfinite(d) ||
        std::abs(d) > 1e6)
      return false;
  }
  out = static_cast<float>(d);
  return true;
}

// Whitespace and '|' separate tokens; '|' (and '|:') is a token of its own.
// A '#' that starts a token starts a comment; inside one (`C#4`) it stays.
std::vector<Tok> tokenize(std::string_view l) {
  std::vector<Tok> out;
  size_t i = 0;
  while (i < l.size()) {
    const char c = l[i];
    if (c == '#') break;
    size_t n = 1;
    if (c == '|') {
      n = i + 1 < l.size() && l[i + 1] == ':' ? 2 : 1;
    } else if (c != ' ' && c != '\t' && c != '\r') {
      n = std::min(l.find_first_of(" \t\r|", i), l.size()) - i;
    } else {
      ++i;
      continue;
    }
    out.push_back({l.substr(i, n), static_cast<int>(i) + 1});
    i += n;
  }
  return out;
}

class Parser {
 public:
  TrkParse run(std::string_view text) {
    size_t at = 0;
    while (at <= text.size() && out_.errors.empty()) {
      const size_t nl = std::min(text.find('\n', at), text.size());
      ++line_;
      statement(tokenize(text.substr(at, nl - at)));
      at = nl + 1;
    }
    if (out_.errors.empty()) finish();
    if (!out_.errors.empty()) out_.song = TrkSong{};
    return std::move(out_);
  }

 private:
  void err(int col, std::string msg) {
    if (out_.errors.empty()) out_.errors.push_back({line_, col, std::move(msg), {}});
  }
  void err(const Tok& t, const std::string& msg) { err(t.col, msg); }

  void statement(const std::vector<Tok>& t) {
    if (t.empty()) return;
    const std::string_view w = t[0].s;
    if (w == "track") return track(t);
    if (w == "pattern") return pattern(t);
    if (w == "order") return order(t);
    int n = 0;
    if (w == "|" || to_int(w, 0, 1 << 30, n)) return row(t);
    err(t[0], "unknown directive '" + std::string(w) +
              "' (directives: track, pattern, order; rows start with '|')" +
              dsl::did_you_mean(w, {"track", "pattern", "order"}));
  }

  // track <name> <field>...
  void track(const std::vector<Tok>& t) {
    if (!pats_.empty()) return err(t[0], "tracks are declared before the first pattern");
    if (tracks_.size() == kTrkTracks) return err(t[0], "at most 8 tracks");
    if (t.size() < 3) return err(t[0], "track needs a name and fields (note, vel, cva, cvb)");
    if (!is_name(t[1].s)) return err(t[1], "track name must be lowercase (a-z, 0-9, _)");
    if (find_named(tracks_, t[1].s) >= 0) return err(t[1], "duplicate track '" + std::string(t[1].s) + "'");
    Track tr{std::string(t[1].s), {}};
    for (size_t i = 2; i < t.size(); ++i) {
      const auto* f = std::find(std::begin(kFields), std::end(kFields), t[i].s);
      if (f == std::end(kFields))
        return err(t[i], "unknown field '" + std::string(t[i].s) + "' (fields: note, vel, cva, cvb)");
      const int id = static_cast<int>(f - std::begin(kFields));
      if (std::count(tr.fields.begin(), tr.fields.end(), id))
        return err(t[i], "field '" + std::string(t[i].s) + "' listed twice");
      tr.fields.push_back(id);
    }
    tracks_.push_back(std::move(tr));
  }

  // pattern <name> <rows> [<track>...]
  void pattern(const std::vector<Tok>& t) {
    if (tracks_.empty()) return err(t[0], "declare tracks before patterns ('track <name> <fields>')");
    if (t.size() < 3) return err(t[0], "pattern needs a name and a row count");
    if (!is_name(t[1].s)) return err(t[1], "pattern name must be lowercase (a-z, 0-9, _)");
    if (find_named(pats_, t[1].s) >= 0) return err(t[1], "duplicate pattern '" + std::string(t[1].s) + "'");
    Pattern p{std::string(t[1].s), 0, {}, {}};
    if (!to_int(t[2].s, 1, kTrkMaxPatternRows, p.rows))
      return err(t[2], "row count must be an integer in 1..1024");
    for (size_t i = 3; i < t.size(); ++i) {
      const int id = find_named(tracks_, t[i].s);
      if (id < 0) return err(t[i], "unknown track '" + std::string(t[i].s) + "'" + hint(tracks_, t[i].s));
      if (std::count(p.tracks.begin(), p.tracks.end(), id))
        return err(t[i], "track '" + std::string(t[i].s) + "' listed twice");
      p.tracks.push_back(id);
    }
    if (p.tracks.empty())
      for (int i = 0; i < static_cast<int>(tracks_.size()); ++i) p.tracks.push_back(i);
    p.ev.resize(static_cast<size_t>(p.rows) * p.tracks.size());
    pats_.push_back(std::move(p));
    next_row_ = 0;
  }

  // [<row>] | <cell> | <cell> ...
  void row(const std::vector<Tok>& t) {
    if (pats_.empty()) return err(t[0], "row outside a pattern (start one with 'pattern <name> <rows>')");
    Pattern& p = pats_.back();
    size_t i = 0;
    int r = next_row_;
    if (t[0].s != "|") {
      if (!to_int(t[0].s, 0, 1 << 30, r)) return err(t[0], "row number must be a decimal integer");
      if (r < next_row_) return err(t[0], "row labels must increase");
      if (t.size() < 2 || t[1].s != "|") return err(t[0], "expected '|' after the row number");
      i = 1;
    }
    if (r >= p.rows)
      return err(t[0], "row " + std::to_string(r) + " is outside pattern '" + p.name + "' (" +
                       std::to_string(p.rows) + " rows: 0.." + std::to_string(p.rows - 1) + ")");
    next_row_ = r + 1;
    cells(t, i, p, r);
  }

  // Splits the tokens after `from` (a '|') into cells and parses each.
  void cells(const std::vector<Tok>& t, size_t from, Pattern& p, int r) {
    size_t col = 0;
    for (size_t i = from; i < t.size() && out_.errors.empty();) {
      if (t[i].s != "|") return err(t[i], "expected '|' between cells");
      size_t end = i + 1;
      while (end < t.size() && t[end].s != "|" && t[end].s != "|:") ++end;
      if (end == i + 1 && end == t.size()) return;  // trailing '|'
      if (col == p.tracks.size())
        return err(t[i], "row has more cells than pattern '" + p.name + "' has tracks (" +
                         std::to_string(p.tracks.size()) + ")");
      cell(t, i + 1, end, tracks_[static_cast<size_t>(p.tracks[col])],
           p.ev[static_cast<size_t>(r) * p.tracks.size() + col]);
      ++col;
      i = end;
    }
  }

  void cell(const std::vector<Tok>& t, size_t from, size_t end, const Track& tr, Event& e) {
    if (end - from > tr.fields.size())
      return err(t[from + tr.fields.size()],
                 "cell has " + std::to_string(end - from) + " values; track '" + tr.name +
                     "' holds " + std::to_string(tr.fields.size()) + " (" + field_list(tr) + ")");
    for (size_t k = from; k < end && out_.errors.empty(); ++k) {
      const int f = tr.fields[k - from];
      if (is_empty(t[k].s)) continue;
      if (f == kNote) note(t[k], e);
      else value(t[k], f, e);
    }
  }

  void note(const Tok& t, Event& e) {
    std::string_view s = t.s;
    if (s == "off" || s == "===") { e.kind = Kind::Off; return; }
    if (s == "x") { e.kind = Kind::Hit; return; }
    const bool slur = s[0] == '~';
    if (slur) s.remove_prefix(1);
    if (to_value(s, e.v[kNote])) { e.kind = slur ? Kind::Slur : Kind::Note; return; }
    if (t.s.find('#') != std::string_view::npos)
      return err(t, "'" + std::string(t.s) + "' is not a note (Cs4 for sharp, Bb3 for flat)");
    err(t, "'" + std::string(t.s) + "' is not a note (uppercase: C4, Cs4, Bb3), number, x, ~note, off or .");
  }

  void value(const Tok& t, int f, Event& e) {
    float v = 0.0f;
    if (!to_value(t.s, v))
      return err(t, "'" + std::string(t.s) + "' is not a number or note name for " + kFields[f]);
    // A note name in a vel slot is almost always fields written out of order.
    if (f == kVel && (std::isupper(static_cast<unsigned char>(t.s[0])) || !(v >= 0.0f && v <= 1.0f)))
      return err(t, "vel must be a number in 0..1 (a cell's values follow the track's field order)");
    e.v[f] = v;
    e.set = static_cast<std::uint8_t>(e.set | (1u << f));
    if (std::isupper(static_cast<unsigned char>(t.s[0]))) e.pitch = static_cast<std::uint8_t>(e.pitch | (1u << f));
  }

  // order <entry>...  with entry = name[+name...][*N], or the loop mark '|:'.
  void order(const std::vector<Tok>& t) {
    for (size_t i = 1; i < t.size() && out_.errors.empty(); ++i) {
      if (t[i].s == "|:") {
        if (loop_entry_ >= 0) return err(t[i], "only one loop point '|:'");
        loop_entry_ = static_cast<int>(entries_.size());
        loop_tok_ = {line_, t[i].col};
        continue;
      }
      entry(t[i]);
    }
  }

  void entry(const Tok& t) {
    Entry e;
    std::string_view s = t.s;
    const size_t star = s.find('*');
    if (star != std::string_view::npos) {
      if (!to_int(s.substr(star + 1), 1, 4096, e.repeat))
        return err(t.col + static_cast<int>(star), "repeat must be '*N' with N in 1..4096");
      s = s.substr(0, star);
    }
    size_t at = 0;
    while (at <= s.size() && out_.errors.empty()) {
      const size_t plus = std::min(s.find('+', at), s.size());
      e.specs.push_back(spec({s.substr(at, plus - at), t.col + static_cast<int>(at)}));
      at = plus + 1;
    }
    entries_.push_back(std::move(e));
    entry_lines_.push_back(line_);
  }

  // One layer: `name` with an optional `^N` transposition in semitones.
  Spec spec(const Tok& t) {
    const size_t hat = std::min(t.s.find('^'), t.s.size());
    Spec sp{{t.s.substr(0, hat), t.col}, 0};
    if (hat < t.s.size() && !to_int(t.s.substr(hat + 1), -48, 48, sp.shift))
      err(t.col + static_cast<int>(hat), "transpose must be '^N' with N in -48..48 semitones");
    return sp;
  }

  // Resolve the order list, then flatten it into song rows.
  void finish() {
    if (entries_.empty()) {
      line_ = 1;
      return err(1, "no order list: nothing would play (add 'order <pattern>...')");
    }
    if (loop_entry_ == static_cast<int>(entries_.size())) {
      line_ = loop_tok_.first;
      return err(loop_tok_.second, "the loop point '|:' needs an order entry after it");
    }
    for (size_t i = 0; i < entries_.size() && out_.errors.empty(); ++i) {
      line_ = entry_lines_[i];
      resolve(entries_[i]);
    }
    if (out_.errors.empty()) flatten();
  }

  void resolve(Entry& e) {
    std::vector<int> owner(tracks_.size(), -1);
    for (const Spec& sp : e.specs) {
      const Tok& n = sp.name;
      const int id = find_named(pats_, n.s);
      if (id < 0) return err(n, "unknown pattern '" + std::string(n.s) + "' in order" + hint(pats_, n.s));
      for (int tr : pats_[static_cast<size_t>(id)].tracks) {
        int& o = owner[static_cast<size_t>(tr)];
        if (o >= 0)
          return err(n, "layers '" + pats_[static_cast<size_t>(o)].name + "' and '" + std::string(n.s) +
                            "' both write track '" + tracks_[static_cast<size_t>(tr)].name + "'");
        o = id;
      }
      e.layers.push_back({id, sp.shift});
    }
  }

  void flatten() {
    TrkSong& song = out_.song;
    song.tracks = static_cast<int>(tracks_.size());
    long total = 0;
    for (size_t i = 0; i < entries_.size(); ++i) {
      if (static_cast<int>(i) == loop_entry_) song.loop = song.steps();
      const Entry& e = entries_[i];
      for (int k = 0; k < e.repeat; ++k) {
        song.step_start.push_back(static_cast<int>(total));
        song.step_pattern.push_back(e.layers[0].pat);
        total += pats_[static_cast<size_t>(e.layers[0].pat)].rows;
        if (total > kTrkMaxRows) {
          line_ = entry_lines_[i];
          return err(e.specs[0].name.col, "song is longer than 65536 rows");
        }
      }
    }
    song.step_start.push_back(static_cast<int>(total));
    song.rows.resize(static_cast<size_t>(total) * tracks_.size());
    TrkRow state[kTrkTracks];
    size_t at = 0;
    for (size_t i = 0; i < entries_.size(); ++i)
      for (int k = 0; k < entries_[i].repeat; ++k) at = play(entries_[i], state, at);
  }

  // Writes one step's rows from `at`; returns the next write position.
  size_t play(const Entry& e, TrkRow* state, size_t at) {
    const int len = pats_[static_cast<size_t>(e.layers[0].pat)].rows;
    for (int r = 0; r < len; ++r) {
      for (size_t tr = 0; tr < tracks_.size(); ++tr) state[tr].retrig = false;
      for (const Layer& l : e.layers) {
        const Pattern& p = pats_[static_cast<size_t>(l.pat)];
        const size_t base = static_cast<size_t>(r % p.rows) * p.tracks.size();
        for (size_t c = 0; c < p.tracks.size(); ++c)
          apply(p.ev[base + c], static_cast<float>(l.shift) / 12.0f, state[p.tracks[c]]);
      }
      for (size_t tr = 0; tr < tracks_.size(); ++tr) out_.song.rows[at++] = state[tr];
    }
    return at;
  }

  // `up` (v/oct) shifts the note and the cv values written as note names.
  static void apply(const Event& e, float up, TrkRow& s) {
    const auto val = [&](int f) { return e.v[f] + ((e.pitch >> f) & 1u ? up : 0.0f); };
    if (e.kind == Kind::Note || e.kind == Kind::Slur) s.voct = e.v[kNote] + up;
    if (e.kind == Kind::Note || e.kind == Kind::Hit) s.retrig = true;
    if (e.kind != Kind::None) s.on = e.kind != Kind::Off;
    if (e.set & (1u << kVel)) s.vel = e.v[kVel];
    if (e.set & (1u << kCva)) s.cva = val(kCva);
    if (e.set & (1u << kCvb)) s.cvb = val(kCvb);
  }

  static std::string field_list(const Track& t) {
    std::vector<std::string> names;
    for (int f : t.fields) names.emplace_back(kFields[f]);
    return dsl::join(names);
  }

  TrkParse out_;
  int line_ = 0, next_row_ = 0, loop_entry_ = -1;
  std::pair<int, int> loop_tok_{0, 0};
  std::vector<Track> tracks_;
  std::vector<Pattern> pats_;
  std::vector<Entry> entries_;
  std::vector<int> entry_lines_;
};

}  // namespace

TrkParse parse_trk(std::string_view text) { return Parser().run(text); }

}  // namespace ms::io
