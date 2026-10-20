#include "graph/voices.h"

#include <algorithm>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#include "dsp/module.h"

namespace ms::graph {
namespace {

struct Group {
  std::string name;
  std::string bound;  // the `notes:` module name
  int n = 1;
  int line = 0, col = 0;
  bool has_member = false;                 // any decl names a member of this group
  std::vector<std::string> members;        // valid member base names, e.g. "lead.osc"
  std::unordered_set<std::string> stems;   // bank stems the bound module exposes
};

using DeclIndex = std::unordered_map<std::string, const dsl::ModuleDecl*>;

const dsl::ParamDecl* find_param(const dsl::ModuleDecl& m, const std::string& name) {
  for (const auto& p : m.params)
    if (p.name == name) return &p;
  return nullptr;
}

// A count read from the AST before any validation has run: the value may be
// huge or non-finite, and a plain cast of an out-of-range double to int is
// undefined behaviour. Saturate instead; the caller clamps to a sane range.
int count_of(const dsl::ParamDecl& p) {
  const double v = p.value.num;
  if (!(v > -1e9)) return -1000000000;  // also catches NaN
  if (v > 1e9) return 1000000000;
  return static_cast<int>(v);
}

// A member instance endpoint, e.g. base "lead.osc", i 0, port "v_oct" ->
// "lead.osc#0.v_oct"; empty port -> "lead.osc#0" (default port, resolved later).
std::string inst_chain(const std::string& base, int i, const std::string& port) {
  std::string s = base + "#" + std::to_string(i);
  if (!port.empty()) { s += '.'; s += port; }
  return s;
}

// Voice count from the bound module's config: `n`, else `voices`, else the
// type default (alloc 4, otherwise 1).
// A negative count also falls back to the default.
int inherited_count(const dsl::ModuleDecl& bound) {
  int n = -1;
  if (const dsl::ParamDecl* pn = find_param(bound, "n")) n = count_of(*pn);
  else if (const dsl::ParamDecl* pv = find_param(bound, "voices")) n = count_of(*pv);
  return n >= 0 ? n : bound.type == "alloc" ? 4 : 1;
}

// Bank stems: the full 8-wide family <stem>1..<stem>8, matching the bank
// convention gate/voct/vel/spread satisfy (voice-alloc-design §3 case 2). A
// <stem>1/<stem>2 pair alone is not enough: midi's cc is only 4-wide
// (cc1..cc4), and requiring just a pair let bare `mid.cc` silently qualify as
// a bank, fanning cc1..ccN across voice instances instead of failing as the
// unknown output it is.
std::unordered_set<std::string> bank_stems(const dsp::ModuleInfo& info) {
  std::unordered_set<std::string> out_names, stems;
  for (const auto& o : info.outputs) out_names.insert(o.name);
  for (const auto& o : info.outputs) {
    const std::string& on = o.name;
    if (on.empty() || on.back() != '1') continue;
    const std::string stem = on.substr(0, on.size() - 1);
    bool full8 = true;
    for (int k = 2; k <= 8 && full8; ++k) full8 = out_names.count(stem + std::to_string(k)) > 0;
    if (full8) stems.insert(stem);
  }
  return stems;
}

class Expander {
 public:
  Expander(dsl::PatchAst& ast, Diags& errors) : ast_(ast), errors_(errors) {}

  void run() {
    const size_t err0 = errors_.size();
    for (const auto& m : ast_.modules) by_name_[m.name] = &m;
    find_groups();
    find_members();
    if (errors_.size() != err0) return;  // leave the AST untouched
    std::vector<dsl::ModuleDecl> modules = stamp_members();
    std::vector<dsl::CableDecl> cables;
    for (const auto& c : ast_.cables) rewrite(c, cables);
    ast_.modules = std::move(modules);
    ast_.cables = std::move(cables);
  }

 private:
  void err(int line, int col, std::string msg) {
    errors_.push_back({line, col, std::move(msg), {}});
  }

  // --- Voice groups: top-level (undotted, or not under another group) -----
  void find_groups() {
    for (const auto& m : ast_.modules) {
      if (m.type != "voice" || nested_in_voice(m.name)) continue;
      Group g;
      g.name = m.name;
      g.line = m.line;
      g.col = m.col;
      bind_notes(m, g);
      groups_.emplace(g.name, std::move(g));
      group_order_.push_back(m.name);
    }
  }

  bool nested_in_voice(const std::string& name) const {
    const auto dot = name.find('.');
    if (dot == std::string::npos) return false;
    auto it = by_name_.find(name.substr(0, dot));
    return it != by_name_.end() && it->second->type == "voice";
  }

  // Resolve the group's `notes:` module: its voice count and bank stems.
  void bind_notes(const dsl::ModuleDecl& voice, Group& g) {
    const dsl::ParamDecl* notes = find_param(voice, "notes");
    if (!notes || notes->value.kind != dsl::Value::Kind::Ident) {
      err(voice.line, voice.col, "voice '" + voice.name + "' requires 'notes:' naming a bank module");
      return;
    }
    g.bound = notes->value.str;
    auto bound_it = by_name_.find(g.bound);
    if (bound_it == by_name_.end()) {
      err(notes->line, notes->col, "voice '" + voice.name + "' notes: unknown module '" + g.bound + "'");
      return;
    }
    const dsl::ModuleDecl& b = *bound_it->second;
    const dsp::ModuleInfo* info = dsp::find_module(b.type);
    if (!info || info->output_index("gate1") < 0) {
      err(notes->line, notes->col, "voice '" + voice.name + "' notes module '" + g.bound +
                                       "' has no voice banks (gate1..)");
      return;
    }
    const int n = inherited_count(b);
    g.n = std::clamp(n, 1, 8);
    g.stems = bank_stems(*info);
    // An explicit n: on the voice decl must match the inherited count.
    if (const dsl::ParamDecl* vn = find_param(voice, "n")) {
      const int voice_n = count_of(*vn);
      if (voice_n != n)
        err(vn->line, vn->col, "voice '" + voice.name + "' n: " + std::to_string(voice_n) +
                                   " does not match notes source n " + std::to_string(n));
    }
  }

  // Longest voice-group name that is a dot-prefix of `name`, or "".
  std::string owning_group(const std::string& name) const {
    std::string best;
    for (const auto& gn : group_order_) {
      if (name.size() > gn.size() + 1 && name.compare(0, gn.size(), gn) == 0 &&
          name[gn.size()] == '.' && gn.size() > best.size())
        best = gn;
    }
    return best;
  }

  // --- Members: dotted decls whose prefix is a declared voice group --------
  // A dotted name whose prefix is NOT a voice group is an ordinary module
  // (spec §1: declared names may contain a dot); it is left untouched here.
  void find_members() {
    for (const auto& m : ast_.modules) {
      if (m.name.find('.') == std::string::npos) continue;  // a top-level decl
      const std::string g = owning_group(m.name);
      if (g.empty()) continue;  // plain dotted module, not a voice member
      groups_[g].has_member = true;  // counts toward membership even if invalid
      if (m.type == "voice") {
        err(m.line, m.col, "voice member '" + m.name + "' may not itself be a voice (nested groups)");
        continue;
      }
      member_group_[m.name] = g;
      groups_[g].members.push_back(m.name);
    }
    for (const auto& gn : group_order_)
      if (!groups_[gn].has_member)
        err(groups_[gn].line, groups_[gn].col, "voice group '" + gn + "' has no members");
  }

  // Each member once per voice as `<member>#<i>`; voice decls dropped.
  std::vector<dsl::ModuleDecl> stamp_members() {
    std::vector<dsl::ModuleDecl> out;
    for (const auto& m : ast_.modules) {
      if (m.type == "voice") continue;  // group decl removed
      auto mg = member_group_.find(m.name);
      if (mg == member_group_.end()) { out.push_back(m); continue; }
      for (int i = 0; i < groups_[mg->second].n; ++i) {
        dsl::ModuleDecl copy = m;
        copy.name = m.name + "#" + std::to_string(i);
        out.push_back(std::move(copy));
      }
    }
    return out;
  }

  // --- Cables ---------------------------------------------------------------
  // Each side of a cable expands to the endpoint chains it stands for; an
  // empty list means "this side is untouched". Same-group and bank-read
  // cables pair instance i with instance i; everything else is the product
  // of the two sides (broadcast, straight sum, or n_src x n_dst across two
  // groups; an ordinary cable is the 1 x 1 product).
  void rewrite(const dsl::CableDecl& c, std::vector<dsl::CableDecl>& out) {
    auto is_decl = [&](const std::string& n) { return by_name_.count(n) > 0; };
    const SplitEndpoint s = c.src.is_literal ? SplitEndpoint{} : split_endpoint(is_decl, c.src.module);
    const SplitEndpoint d = split_endpoint(is_decl, c.dst.module);
    const std::string sgroup = group_of(s), dgroup = group_of(d);
    std::vector<std::string> srcs = instances(s, sgroup), dsts = instances(d, dgroup);
    bool paired = !sgroup.empty() && sgroup == dgroup;
    if (sgroup.empty() && !dgroup.empty() && is_bank_read(s, groups_.at(dgroup))) {
      // Bound module's bare bank name read into a member. Bank ports are
      // 1-based; instance i binds to port index i+1.
      for (int i = 0; i < groups_.at(dgroup).n; ++i)
        srcs.push_back(s.module + "." + s.port + std::to_string(i + 1));
      paired = true;
    }
    auto emit = [&](const std::string* src, const std::string* dst) {
      dsl::CableDecl nc = c;
      if (src) nc.src.module = *src;
      if (dst) nc.dst.module = *dst;
      out.push_back(std::move(nc));
    };
    if (paired) {
      for (size_t i = 0; i < dsts.size(); ++i) emit(&srcs[i], &dsts[i]);
      return;
    }
    const size_t ns = std::max<size_t>(srcs.size(), 1), nd = std::max<size_t>(dsts.size(), 1);
    for (size_t i = 0; i < ns; ++i)
      for (size_t j = 0; j < nd; ++j)
        emit(srcs.empty() ? nullptr : &srcs[i], dsts.empty() ? nullptr : &dsts[j]);
  }

  std::string group_of(const SplitEndpoint& s) const {
    if (!s.ok) return "";
    auto it = member_group_.find(s.module);
    return it == member_group_.end() ? "" : it->second;
  }

  std::vector<std::string> instances(const SplitEndpoint& s, const std::string& group) const {
    std::vector<std::string> out;
    if (group.empty()) return out;
    for (int i = 0; i < groups_.at(group).n; ++i) out.push_back(inst_chain(s.module, i, s.port));
    return out;
  }

  static bool is_bank_read(const SplitEndpoint& s, const Group& g) {
    return s.ok && s.module == g.bound && g.stems.count(s.port) > 0;
  }

  dsl::PatchAst& ast_;
  Diags& errors_;
  DeclIndex by_name_;  // pre-expansion decls, also the split_endpoint index
  std::unordered_map<std::string, Group> groups_;
  std::vector<std::string> group_order_;
  std::unordered_map<std::string, std::string> member_group_;  // member base -> group
};

}  // namespace

void expand_voices(dsl::PatchAst& ast, Diags& errors) { Expander(ast, errors).run(); }

}  // namespace ms::graph
