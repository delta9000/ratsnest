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

enum Field { kNote, kVel, kCva, kCvb, kCvd };
constexpr const char* kFields[] = {"note", "vel", "cva", "cvb", "cvd"};
enum class Kind : std::uint8_t { None, Note, Slur, Off, Hit };

struct Tok { std::string_view s; int col; };
constexpr int kMaxLen = 4096;  // note length in rows
// `len`: rows a note sounds before its gate drops; 0 holds until changed.
// `src`: the earlier track whose note, cva, cvb, cvd `t1`..`t4` read; -1 none.
struct Track { std::string name; std::vector<int> fields; int len = 0, src = -1; };

bool has(const Track& t, int f) { return std::count(t.fields.begin(), t.fields.end(), f) > 0; }

// `t1`..`t4`, optionally offset: a chord tone of the source track.
bool is_tone(std::string_view s) { return s.size() >= 2 && s[0] == 't' && s[1] >= '1' && s[1] <= '4'; }
// One cell as written: a note event plus the value fields it sets (bit f of
// `set` for v[f]); bit f of `pitch` marks a cv value written as a note name,
// which an order-list `^N` transposes along with the note. `len` is the
// note's length in rows (`C4:3`, else the track's); 0 holds. `tone[f]` 1..4
// makes v[f] an offset from the source track's note, cva, cvb or cvd.
struct Event {
  Kind kind = Kind::None;
  std::uint8_t set = 0, pitch = 0;
  std::int16_t len = 0;
  std::uint8_t tone[5] = {};
  float v[5] = {};
};
struct Pattern {
  std::string name;
  int rows = 0;
  std::vector<int> tracks;
  std::vector<Event> ev;  // row * tracks.size() + column
  int bar = 0;            // rows per bar (`4x16`); 0: no bars
};
// A layer as written in the order list (`name^N`), then resolved.
// `at` is the `@R` text (col 0: none). `outer` is a section's `^N`, which
// moves written pitches but not chord tones: those follow their source.
// `free` is the column of a `~` (0: none).
struct Spec { Tok name; int shift = 0; Tok at{{}, 0}; int outer = 0; int free = 0; };
// `at`: the step row a one-shot overlay starts on; -1 loops within the
// step; kFree loops on the song row, so its phase carries across steps.
constexpr int kFree = -2;
struct Layer { int pat = 0; int shift = 0; int at = -1; int outer = 0; };
struct Entry { std::vector<Spec> specs; int repeat = 1; std::vector<Layer> layers; };
// `section <name> <layers>`: a named layer group for the order list.
struct Section { std::string name; std::vector<Spec> specs; int line = 0; };
// A `.2>` ramp: field `f` of cell `cell` (row * width + column) in pattern
// `pat` glides to the field's next value; line/col locate the token.
struct Ramp { size_t pat, cell; int f, line, col; };
// A track's state while flattening: the row so far and the song row its
// sounding note's length runs out (-1: none).
struct Voice { TrkRow s; int off_at = -1; };
// The event a track plays on one row, and the layer it comes from.
struct Cue { const Event* e = nullptr; const Layer* l = nullptr; };

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
    using Directive = void (Parser::*)(const std::vector<Tok>&);
    static constexpr std::pair<std::string_view, Directive> kDirectives[] = {
        {"track", &Parser::track}, {"pattern", &Parser::pattern}, {"lane", &Parser::lane},
        {"section", &Parser::section}, {"order", &Parser::order}};
    const std::string_view w = t[0].s;
    for (const auto& [name, fn] : kDirectives)
      if (w == name) return (this->*fn)(t);
    if (w == "|" || std::isdigit(static_cast<unsigned char>(w[0]))) return row(t);
    err(t[0], "unknown directive '" + std::string(w) +
              "' (directives: track, pattern, lane, section, order; rows start with '|')" +
              dsl::did_you_mean(w, {"track", "pattern", "lane", "section", "order"}));
  }

  // track <name> <field>... [len <rows>] [from <track>]
  void track(const std::vector<Tok>& t) {
    if (!pats_.empty()) return err(t[0], "tracks are declared before the first pattern");
    if (tracks_.size() == kTrkTracks) return err(t[0], "at most " + std::to_string(kTrkTracks) + " tracks");
    if (t.size() < 3) return err(t[0], "track needs a name and fields (note, vel, cva, cvb, cvd)");
    if (!is_name(t[1].s)) return err(t[1], "track name must be lowercase (a-z, 0-9, _)");
    if (find_named(tracks_, t[1].s) >= 0) return err(t[1], "duplicate track '" + std::string(t[1].s) + "'");
    Track tr{std::string(t[1].s), {}};
    for (size_t i = 2; i < t.size() && out_.errors.empty(); ++i) {
      if (t[i].s == "len" || t[i].s == "from") {
        option(t, i++, tr);
        continue;
      }
      const auto* f = std::find(std::begin(kFields), std::end(kFields), t[i].s);
      if (f == std::end(kFields))
        return err(t[i], "unknown field '" + std::string(t[i].s) + "' (fields: note, vel, cva, cvb, cvd)");
      const int id = static_cast<int>(f - std::begin(kFields));
      if (has(tr, id))
        return err(t[i], "field '" + std::string(t[i].s) + "' listed twice");
      tr.fields.push_back(id);
    }
    if (tr.fields.empty()) err(t[0], "track needs a name and fields (note, vel, cva, cvb, cvd)");
    tracks_.push_back(std::move(tr));
  }

  // `len <rows>` or `from <track>` among a track's fields.
  void option(const std::vector<Tok>& t, size_t i, Track& tr) {
    if (i + 1 == t.size())
      return err(t[i], "'" + std::string(t[i].s) + "' needs a value ('len 1' rows, 'from <track>')");
    const Tok& v = t[i + 1];
    if (t[i].s == "len") {
      if (!to_int(v.s, 1, kMaxLen, tr.len)) err(v, "len must be a row count in 1..4096");
      return;
    }
    tr.src = find_named(tracks_, v.s);
    if (tr.src < 0)
      return err(v, "unknown track '" + std::string(v.s) + "' (a source track is declared earlier)" +
                        hint(tracks_, v.s));
    if (!has(tracks_[static_cast<size_t>(tr.src)], kNote))
      err(v, "track '" + std::string(v.s) + "' has no note field to read chord tones from");
  }

  // pattern <name> <rows|BARSxROWS> [<track>...]
  void pattern(const std::vector<Tok>& t) {
    if (tracks_.empty()) return err(t[0], "declare tracks before patterns ('track <name> <fields>')");
    if (t.size() < 3) return err(t[0], "pattern needs a name and a row count");
    if (!is_name(t[1].s)) return err(t[1], "pattern name must be lowercase (a-z, 0-9, _)");
    if (taken(t[1])) return;
    Pattern p{std::string(t[1].s), 0, {}, {}};
    if (!size(t[2], p)) return;
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
      if (!to_row(t[0], p, r)) return;
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
    e.len = static_cast<std::int16_t>(tr.len);
    for (size_t k = from; k < end && out_.errors.empty(); ++k) {
      const int f = tr.fields[k - from];
      if (is_empty(t[k].s)) continue;
      if (written(e, f)) return err(t[k], "track '" + tr.name + "' on this row is already written by a lane (put '.' in the lane at this row to write it here)");
      if (f == kNote) note(t[k], tr, e);
      else value(t[k], tr, f, e);
    }
  }

  static bool written(const Event& e, int f) { return f == kNote ? e.kind != Kind::None : (e.set >> f) & 1u; }

  // lane <track> <steps>: one character per row from row 0, spaces ignored.
  void lane(const std::vector<Tok>& t) {
    if (pats_.empty()) return err(t[0], "lane outside a pattern (start one with 'pattern <name> <rows>')");
    if (t.size() < 3) return err(t[0], "lane needs a track and its steps ('lane kick x...x...')");
    Pattern& p = pats_.back();
    const int id = find_named(tracks_, t[1].s);
    const auto col = std::find(p.tracks.begin(), p.tracks.end(), id);
    if (col == p.tracks.end())
      return err(t[1], "track '" + std::string(t[1].s) + "' is not in pattern '" + p.name + "'" +
                           hint(tracks_, t[1].s));
    int r = 0;
    for (size_t i = 2; i < t.size(); ++i)
      for (size_t k = 0; k < t[i].s.size() && out_.errors.empty(); ++k)
        step({t[i].s.substr(k, 1), t[i].col + static_cast<int>(k)}, r++, p,
             static_cast<size_t>(col - p.tracks.begin()));
  }

  // One lane character: `.` rest, `x` hit, `1`-`9` hit at vel n/9, `0` off.
  void step(const Tok& c, int r, Pattern& p, size_t col) {
    if (r >= p.rows)
      return err(c, "lane is longer than pattern '" + p.name + "' (" + std::to_string(p.rows) + " rows)");
    const size_t kind = std::string_view(".0x123456789").find(c.s[0]);
    if (kind == std::string_view::npos)
      return err(c, "'" + std::string(c.s) + "' is not a lane step (. rest, x hit, 1-9 hit at vel n/9, 0 off)");
    const Track& tr = tracks_[static_cast<size_t>(p.tracks[col])];
    Event& e = p.ev[static_cast<size_t>(r) * p.tracks.size() + col];
    const bool vel = kind > 2 && has(tr, kVel);
    if (kind == 0) return;
    if (written(e, kNote) || (vel && written(e, kVel)))
      return err(c, "row " + std::to_string(r) + " of track '" + tr.name + "' is already written");
    e.kind = kind == 1 ? Kind::Off : Kind::Hit;
    e.len = static_cast<std::int16_t>(tr.len);
    if (!vel) return;
    e.v[kVel] = static_cast<float>(kind - 2) / 9.0f;
    e.set = static_cast<std::uint8_t>(e.set | (1u << kVel));
  }

  void note(const Tok& t, const Track& tr, Event& e) {
    std::string_view s = t.s.substr(0, t.s.find(':'));
    if (s.size() < t.s.size() && !length(t, s.size(), e)) return;
    if (s == "off" || s == "===") { e.kind = Kind::Off; return; }
    if (s == "x") { e.kind = Kind::Hit; return; }
    const bool slur = !s.empty() && s[0] == '~';
    if (slur) s.remove_prefix(1);
    e.kind = slur ? Kind::Slur : Kind::Note;
    e.pitch = static_cast<std::uint8_t>(e.pitch | (1u << kNote));
    if (is_tone(s)) return tone(t, s, tr, kNote, e);
    if (to_value(s, e.v[kNote])) return;
    e.kind = Kind::None;
    if (t.s.find('#') != std::string_view::npos)
      return err(t, "'" + std::string(t.s) + "' is not a note (Cs4 for sharp, Bb3 for flat)");
    err(t, "'" + std::string(t.s) + "' is not a note (uppercase: C4, Cs4, Bb3), number, x, ~note, t1..t4 (with 'from'), off or .");
  }

  // `:L` at `colon` in a note token: the note sounds L rows.
  bool length(const Tok& t, size_t colon, Event& e) {
    int len = 0;
    if (to_int(t.s.substr(colon + 1), 1, kMaxLen, len)) return e.len = static_cast<std::int16_t>(len), true;
    err(t.col + static_cast<int>(colon), "note length must be ':L' with L in 1..4096 rows (C4:2)");
    return false;
  }

  // `t2+7` in field `f`: source tone 2 (its cva) up 7 semitones.
  void tone(const Tok& t, std::string_view s, const Track& tr, int f, Event& e) {
    const std::string name(s.substr(0, 2));
    if (tr.src < 0)
      return err(t, "'" + name + "' is a chord tone, but track '" + tr.name +
                        "' has no source (declare 'track " + tr.name + " ... from <track>')");
    const Track& src = tracks_[static_cast<size_t>(tr.src)];
    const int k = s[1] - '0', sf = k == 1 ? kNote : k;  // t1 note, t2 cva, t3 cvb, t4 cvd
    if (!has(src, sf))
      return err(t, "'" + name + "' reads " + kFields[sf] + ", which track '" + src.name + "' does not hold");
    std::string_view off = s.substr(2);
    if (!off.empty() && off[0] == '+') off.remove_prefix(1);
    int n = 0;
    if (s.size() > 2 && !to_int(off, -48, 48, n))
      return err(t, "'" + std::string(s) + "' is not a chord tone (t1..t4 with an optional semitone offset: t1-12, t3+7)");
    e.tone[f] = static_cast<std::uint8_t>(k);
    e.v[f] = static_cast<float>(n) / 12.0f;
    e.set = static_cast<std::uint8_t>(e.set | (1u << f));
    e.pitch = static_cast<std::uint8_t>(e.pitch | (1u << f));
  }

  void value(const Tok& t, const Track& tr, int f, Event& e) {
    if (f != kVel && is_tone(t.s)) return tone(t, t.s, tr, f, e);
    float v = 0.0f;
    const bool ramp = t.s.size() > 1 && t.s.back() == '>';
    if (!to_value(t.s.substr(0, t.s.size() - ramp), v))
      return err(t, "'" + std::string(t.s) + "' is not a number or note name for " + kFields[f]);
    const Pattern& p = pats_.back();
    if (ramp) ramps_.push_back({pats_.size() - 1, static_cast<size_t>(&e - p.ev.data()), f, line_, t.col});
    // A note name in a vel slot is almost always fields written out of order.
    if (f == kVel && (std::isupper(static_cast<unsigned char>(t.s[0])) || !(v >= 0.0f && v <= 1.0f)))
      return err(t, "vel must be a number in 0..1 (a cell's values follow the track's field order)");
    e.v[f] = v;
    e.set = static_cast<std::uint8_t>(e.set | (1u << f));
    if (std::isupper(static_cast<unsigned char>(t.s[0]))) e.pitch = static_cast<std::uint8_t>(e.pitch | (1u << f));
  }

  // order <entry>...  with entry = layer[+layer...][*N], layer = name[^N][@R],
  // or the loop mark '|:'.
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
    group({s, t.col}, e.specs);
    entries_.push_back(std::move(e));
    entry_lines_.push_back(line_);
  }

  // section <name> <layers>
  void section(const std::vector<Tok>& t) {
    if (t.size() != 3) return err(t[0], "section needs a name and one layer group ('section verse a+b^2+c')");
    if (!is_name(t[1].s)) return err(t[1], "section name must be lowercase (a-z, 0-9, _)");
    if (taken(t[1])) return;
    sections_.push_back({std::string(t[1].s), {}, line_});
    group(t[2], sections_.back().specs);
  }

  // Patterns and sections share one namespace.
  bool taken(const Tok& n) {
    if (find_named(pats_, n.s) < 0 && find_named(sections_, n.s) < 0) return false;
    return err(n, "duplicate name '" + std::string(n.s) + "' (patterns and sections share names)"), true;
  }

  // `a+b^2+c@8`: layers joined by '+'.
  void group(const Tok& t, std::vector<Spec>& out) {
    size_t at = 0;
    while (at <= t.s.size() && out_.errors.empty()) {
      const size_t plus = std::min(t.s.find('+', at), t.s.size());
      out.push_back(spec({t.s.substr(at, plus - at), t.col + static_cast<int>(at)}));
      at = plus + 1;
    }
  }

  // One layer: `name` with an optional `^N` transposition in semitones,
  // `@R` start row and `~` free-running mark, in any order.
  Spec spec(const Tok& t) {
    size_t cut = std::min(t.s.find_first_of("^@~"), t.s.size());
    Spec sp{{t.s.substr(0, cut), t.col}};
    while (cut < t.s.size() && out_.errors.empty()) {
      const size_t next = std::min(t.s.find_first_of("^@~", cut + 1), t.s.size());
      const Tok arg{t.s.substr(cut + 1, next - cut - 1), t.col + static_cast<int>(cut) + 1};
      if (t.s[cut] == '@') sp.at = arg;
      else if (t.s[cut] == '~') sp.free = arg.s.empty() ? arg.col - 1 : (err(arg, "'~' takes no value (a free-running layer is 'name~')"), 0);
      else if (!to_int(arg.s, -48, 48, sp.shift))
        err(arg.col - 1, "transpose must be '^N' with N in -48..48 semitones");
      cut = next;
    }
    return sp;
  }

  // Resolve the order list, then flatten it into song rows.
  void finish() {
    for (size_t i = 0; i < ramps_.size() && out_.errors.empty(); ++i) ramp(ramps_[i]);
    if (!out_.errors.empty()) return;
    if (entries_.empty()) {
      line_ = 1;
      return err(1, "no order list: nothing would play (add 'order <pattern>...')");
    }
    if (loop_entry_ == static_cast<int>(entries_.size())) {
      line_ = loop_tok_.first;
      return err(loop_tok_.second, "the loop point '|:' needs an order entry after it");
    }
    for (size_t i = 0; i < sections_.size() && out_.errors.empty(); ++i) {
      line_ = sections_[i].line;
      Entry e{sections_[i].specs, 1, {}};
      resolve(e);
    }
    for (size_t i = 0; i < entries_.size() && out_.errors.empty(); ++i) {
      line_ = entry_lines_[i];
      expand(entries_[i]);
      resolve(entries_[i]);
    }
    if (out_.errors.empty()) flatten();
  }

  // Fills the rows between a ramp and the field's next value in its pattern.
  void ramp(const Ramp& rp) {
    Pattern& p = pats_[rp.pat];
    const size_t w = p.tracks.size(), bit = 1u << rp.f;
    size_t end = rp.cell + w;
    while (end < p.ev.size() && !written(p.ev[end], rp.f)) end += w;
    line_ = rp.line;
    if (end >= p.ev.size() || p.ev[end].tone[rp.f])
      return err(rp.col, std::string("a ramp '>' glides to the next number or note written in its ") +
                             kFields[rp.f] + " field later in pattern '" + p.name + "'; none follows");
    const Event &a = p.ev[rp.cell], &b = p.ev[end];
    const float n = static_cast<float>((end - rp.cell) / w);
    for (size_t at = rp.cell + w; at < end; at += w) {
      Event& e = p.ev[at];
      e.v[rp.f] = a.v[rp.f] + (b.v[rp.f] - a.v[rp.f]) * static_cast<float>((at - rp.cell) / w) / n;
      e.set = static_cast<std::uint8_t>(e.set | bit);
      e.pitch = static_cast<std::uint8_t>(e.pitch | (a.pitch & b.pitch & bit));
    }
  }

  // Splices each section into its layers; `verse^2` adds 2 to every one.
  void expand(Entry& e) {
    std::vector<Spec> out;
    for (const Spec& sp : e.specs) {
      const int id = find_named(sections_, sp.name.s);
      if (id < 0) {
        out.push_back(sp);
        continue;
      }
      if (sp.at.col) return err(sp.at.col - 1, "a section cannot be an '@' overlay (overlay a pattern)");
      for (Spec in : sections_[static_cast<size_t>(id)].specs) {
        in.outer += sp.shift;
        in.name.col = sp.name.col;  // errors point at the section's use
        if (in.at.col) in.at.col = sp.name.col + 1;
        out.push_back(in);
      }
    }
    e.specs = std::move(out);
  }

  void resolve(Entry& e) {
    std::vector<int> owner(tracks_.size(), -1);
    for (const Spec& sp : e.specs) {
      const Tok& n = sp.name;
      const int id = find_named(pats_, n.s);
      if (id < 0) return err(n, "unknown pattern '" + std::string(n.s) + "'" + unknown(n.s));
      Layer l{id, sp.shift, sp.free ? kFree : -1, sp.outer};
      if (sp.free && !runs_free(e, sp)) return;
      if (sp.at.col ? !overlay(e, sp, l) : !(own(owner, n, id) && (sp.free || fits(e, n, id)))) return;
      e.layers.push_back(l);
    }
  }

  // Why a layer name is not a pattern: a nested section, or a typo.
  std::string unknown(std::string_view n) const {
    if (find_named(sections_, n) >= 0) return " (a section holds patterns, not sections)";
    std::vector<std::string> names;
    for (const auto& p : pats_) names.push_back(p.name);
    for (const auto& x : sections_) names.push_back(x.name);
    return dsl::did_you_mean(n, names);
  }

  // Claims pattern `id`'s tracks for a looping layer.
  bool own(std::vector<int>& owner, const Tok& n, int id) {
    for (int tr : pats_[static_cast<size_t>(id)].tracks) {
      int& o = owner[static_cast<size_t>(tr)];
      if (o >= 0)
        return err(n, "layers '" + pats_[static_cast<size_t>(o)].name + "' and '" + std::string(n.s) +
                          "' both write track '" + tracks_[static_cast<size_t>(tr)].name +
                          "' (only an '@' overlay may share tracks)"),
               false;
      o = id;
    }
    return true;
  }

  // A looping layer longer than the step would lose its tail unheard.
  bool fits(const Entry& e, const Tok& n, int id) {
    if (e.layers.empty()) return true;
    const Pattern &first = pats_[static_cast<size_t>(e.layers[0].pat)], &p = pats_[static_cast<size_t>(id)];
    if (p.rows <= first.rows) return true;
    return err(n, "layer '" + p.name + "' (" + std::to_string(p.rows) + " rows) is longer than the step (" +
                      std::to_string(first.rows) + " rows, set by '" + first.name +
                      "'); list the longest layer first"),
           false;
  }

  // `name~` loops on the song row, so it may outlast the step; it cannot set
  // the step's length or be a one-shot.
  bool runs_free(const Entry& e, const Spec& sp) {
    if (sp.at.col) return err(sp.free, "a layer is either free-running '~' or a one-shot '@' overlay, not both"), false;
    if (!e.layers.empty()) return true;
    return err(sp.free, "the first layer sets the step length; put a free-running '~' layer after it"), false;
  }

  // `name@R`: plays once from row R of the step, over the layers before it.
  bool overlay(const Entry& e, const Spec& sp, Layer& l) {
    if (e.layers.empty())
      return err(sp.at.col - 1, "the first layer sets the step length; put an '@' overlay after it"), false;
    const Pattern& first = pats_[static_cast<size_t>(e.layers[0].pat)];
    if (!to_row(sp.at, first, l.at)) return false;
    if (l.at < first.rows) return true;
    return err(sp.at, "overlay start " + std::to_string(l.at) + " is past the step's end ('" + first.name +
                          "' has " + std::to_string(first.rows) + " rows: 0.." + std::to_string(first.rows - 1) + ")"),
           false;
  }

  // A row position written in pattern `p`'s terms.
  bool to_row(const Tok& t, const Pattern& p, int& out) {
    const size_t colon = t.s.find(':');
    if (colon == std::string_view::npos && to_int(t.s, 0, 1 << 30, out)) return true;
    if (colon == std::string_view::npos) return err(t, "row number must be a decimal integer or bar:row"), false;
    if (!p.bar)
      return err(t, "'" + std::string(t.s) + "' is bar:row, but pattern '" + p.name +
                        "' has no bars (declare 'pattern " + p.name + " BARSxROWS', e.g. 4x16)"),
             false;
    int b = 0, r = 0;
    if (to_int(t.s.substr(0, colon), 1, p.rows / p.bar, b) && to_int(t.s.substr(colon + 1), 0, p.bar - 1, r))
      return out = (b - 1) * p.bar + r, true;
    return err(t, "'" + std::string(t.s) + "' is not a bar:row of pattern '" + p.name + "' (bars 1.." +
                      std::to_string(p.rows / p.bar) + ", rows 0.." + std::to_string(p.bar - 1) + ")"),
           false;
  }

  // A pattern's length: `64` rows, or `4x16` bars of rows.
  bool size(const Tok& t, Pattern& p) {
    const size_t x = t.s.find('x');
    int bars = 1;
    const bool ok = x == std::string_view::npos
                        ? to_int(t.s, 1, kTrkMaxPatternRows, p.rows)
                        : to_int(t.s.substr(0, x), 1, kTrkMaxPatternRows, bars) &&
                              to_int(t.s.substr(x + 1), 1, kTrkMaxPatternRows, p.bar);
    if (p.bar) p.rows = bars * p.bar;
    if (ok && p.rows <= kTrkMaxPatternRows) return true;
    return err(t, "row count must be an integer in 1..1024, or BARSxROWS (4x16) of at most 1024 rows"), false;
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
    size_t at = 0;
    for (size_t i = 0; i < entries_.size(); ++i)
      for (int k = 0; k < entries_[i].repeat; ++k) at = play(entries_[i], at);
  }

  // Writes one step's rows from `at`; returns the next write position.
  size_t play(const Entry& e, size_t at) {
    const int len = pats_[static_cast<size_t>(e.layers[0].pat)].rows;
    for (int r = 0; r < len; ++r) {
      Cue cue[kTrkTracks];
      const int row = static_cast<int>(at / tracks_.size());
      for (const Layer& l : e.layers) cues(l, l.at == kFree ? row : r, cue);
      for (size_t tr = 0; tr < tracks_.size(); ++tr) {
        tick(tr, cue[tr], row);
        out_.song.rows[at++] = voice_[tr].s;
      }
    }
    return at;
  }

  // What layer `l` plays on step row `r` (song row, if free), per track.
  // An overlay plays only within its span, and only its non-empty events.
  void cues(const Layer& l, int r, Cue* cue) const {
    const Pattern& p = pats_[static_cast<size_t>(l.pat)];
    const int pr = l.at < 0 ? r % p.rows : r - l.at;
    if (pr < 0 || pr >= p.rows) return;
    const size_t base = static_cast<size_t>(pr) * p.tracks.size();
    for (size_t c = 0; c < p.tracks.size(); ++c) {
      const Event& ev = p.ev[base + c];
      if (l.at < 0 || ev.kind != Kind::None || ev.set) cue[p.tracks[c]] = {&ev, &l};
    }
  }


  // Track `tr` on song row `row`: a note whose length ran out ends, then
  // the row's event applies.
  void tick(size_t tr, const Cue& c, int row) {
    Voice& v = voice_[tr];
    v.s.retrig = false;
    if (v.off_at == row) v.s.on = false;
    if (!c.e) return;
    const int src = tracks_[tr].src;
    apply(*c.e, *c.l, voice_[src < 0 ? tr : static_cast<size_t>(src)].s, v.s);
    if (c.e->kind == Kind::None) return;
    v.off_at = c.e->kind != Kind::Off && c.e->len > 0 ? row + c.e->len : -1;
  }

  // The layer's `^N` shifts the note and the cv values written as pitches;
  // chord tones add to the source track's resolved values on this row, so
  // an enclosing section's `^N` (already in the source) skips them.
  static void apply(const Event& e, const Layer& l, const TrkRow& src, TrkRow& s) {
    const float tones[5] = {0.0f, src.voct, src.cva, src.cvb, src.cvd};
    const float own = static_cast<float>(l.shift) / 12.0f, all = own + static_cast<float>(l.outer) / 12.0f;
    const auto up = [&](int f) { return (e.pitch >> f) & 1u ? (e.tone[f] ? own : all) : 0.0f; };
    const auto val = [&](int f) { return tones[e.tone[f]] + e.v[f] + up(f); };
    if (e.kind == Kind::Note || e.kind == Kind::Slur) s.voct = val(kNote);

    if (e.kind == Kind::Note || e.kind == Kind::Hit) s.retrig = true;
    if (e.kind != Kind::None) s.on = e.kind != Kind::Off;
    if (e.set & (1u << kVel)) s.vel = e.v[kVel];
    float* const cv[] = {&s.cva, &s.cvb, &s.cvd};
    for (int f = kCva; f <= kCvd; ++f)
      if (e.set & (1u << f)) *cv[f - kCva] = val(f);
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
  Voice voice_[kTrkTracks];
  std::vector<Pattern> pats_;
  std::vector<Entry> entries_;
  std::vector<int> entry_lines_;
  std::vector<Section> sections_;
  std::vector<Ramp> ramps_;


};

}  // namespace

TrkParse parse_trk(std::string_view text) { return Parser().run(text); }

}  // namespace ms::io
