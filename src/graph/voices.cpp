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

const dsl::ParamDecl* find_param(const dsl::ModuleDecl& m, const std::string& name) {
  for (const auto& p : m.params)
    if (p.name == name) return &p;
  return nullptr;
}

// A member instance endpoint, e.g. base "lead.osc", i 0, port "v_oct" ->
// "lead.osc#0.v_oct"; empty port -> "lead.osc#0" (default port, resolved later).
std::string inst_chain(const std::string& base, int i, const std::string& port) {
  std::string s = base + "#" + std::to_string(i);
  if (!port.empty()) { s += '.'; s += port; }
  return s;
}

}  // namespace

void expand_voices(dsl::PatchAst& ast, Diags& errors) {
  const size_t err0 = errors.size();
  auto err = [&](int line, int col, std::string msg) {
    errors.push_back({line, col, std::move(msg)});
  };

  // --- Index decls; identify voice groups (top-level, i.e. undotted) --------
  std::unordered_map<std::string, const dsl::ModuleDecl*> by_name;
  for (const auto& m : ast.modules) by_name[m.name] = &m;

  std::unordered_map<std::string, Group> groups;
  std::vector<std::string> group_order;  // deterministic diagnostics/order
  for (const auto& m : ast.modules) {
    if (m.type != "voice") continue;
    // A dotted voice decl is a nested-voice member; handled in the member pass.
    if (m.name.find('.') != std::string::npos) continue;

    Group g;
    g.name = m.name;
    g.line = m.line;
    g.col = m.col;

    const dsl::ParamDecl* notes = find_param(m, "notes");
    if (!notes || notes->value.kind != dsl::Value::Kind::Ident) {
      err(m.line, m.col, "voice '" + m.name + "' requires 'notes:' naming a bank module");
    } else {
      g.bound = notes->value.str;
      auto bound_it = by_name.find(g.bound);
      if (bound_it == by_name.end()) {
        err(notes->line, notes->col,
            "voice '" + m.name + "' notes: unknown module '" + g.bound + "'");
      } else {
        const dsl::ModuleDecl& b = *bound_it->second;
        const dsp::ModuleInfo* info = dsp::find_module(b.type);
        if (!info || info->output_index("gate1") < 0) {
          err(notes->line, notes->col, "voice '" + m.name + "' notes module '" + g.bound +
                                           "' has no voice banks (gate1..)");
        } else {
          // Voice count from the bound module's config: `n`, else `voices`,
          // else the type default (alloc 4, otherwise 1).
          int n = -1;
          if (const dsl::ParamDecl* pn = find_param(b, "n")) n = static_cast<int>(pn->value.num);
          else if (const dsl::ParamDecl* pv = find_param(b, "voices")) n = static_cast<int>(pv->value.num);
          if (n < 0) n = (b.type == "alloc") ? 4 : 1;
          g.n = std::clamp(n, 1, 8);

          // Bank stems: the full 8-wide family <stem>1..<stem>8, matching the
          // bank convention gate/voct/vel/spread satisfy (voice-alloc-design
          // §3 case 2). A <stem>1/<stem>2 pair alone is not enough: midi's
          // cc is only 4-wide (cc1..cc4), and requiring just a pair let bare
          // `mid.cc` silently qualify as a bank, fanning cc1..ccN across
          // voice instances instead of failing as the unknown output it is.
          std::unordered_set<std::string> out_names;
          for (const auto& o : info->outputs) out_names.insert(o.name);
          for (const auto& o : info->outputs) {
            std::string on = o.name;
            if (!on.empty() && on.back() == '1') {
              std::string stem = on.substr(0, on.size() - 1);
              bool full8 = true;
              for (int k = 2; k <= 8; ++k)
                if (!out_names.count(stem + std::to_string(k))) { full8 = false; break; }
              if (full8) g.stems.insert(stem);
            }
          }

          // An explicit n: on the voice decl must match the inherited count.
          if (const dsl::ParamDecl* vn = find_param(m, "n")) {
            int voice_n = static_cast<int>(vn->value.num);
            if (voice_n != n)
              err(vn->line, vn->col, "voice '" + m.name + "' n: " + std::to_string(voice_n) +
                                         " does not match notes source n " + std::to_string(n));
          }
        }
      }
    }
    groups.emplace(g.name, std::move(g));
    group_order.push_back(m.name);
  }

  // Longest voice-group name that is a dot-prefix of `name`, or "".
  auto owning_group = [&](const std::string& name) -> std::string {
    std::string best;
    for (const auto& gn : group_order) {
      if (name.size() > gn.size() + 1 && name.compare(0, gn.size(), gn) == 0 &&
          name[gn.size()] == '.' && gn.size() > best.size())
        best = gn;
    }
    return best;
  };

  // --- Members: dotted decls whose prefix is a declared voice group ----------
  // A dotted name whose prefix is NOT a voice group is an ordinary module
  // (spec §1: declared names may contain a dot); it is left untouched here.
  std::unordered_map<std::string, std::string> member_group;  // member base -> group
  for (const auto& m : ast.modules) {
    if (m.name.find('.') == std::string::npos) continue;  // a top-level decl
    std::string g = owning_group(m.name);
    if (g.empty()) continue;  // plain dotted module, not a voice member
    groups[g].has_member = true;  // counts toward membership even if invalid
    if (m.type == "voice") {
      err(m.line, m.col,
          "voice member '" + m.name + "' may not itself be a voice (nested groups)");
      continue;
    }
    member_group[m.name] = g;
    groups[g].members.push_back(m.name);
  }

  for (const auto& gn : group_order)
    if (!groups[gn].has_member) err(groups[gn].line, groups[gn].col,
                                    "voice group '" + gn + "' has no members");

  // Leave the AST untouched if anything was rejected.
  if (errors.size() != err0) return;

  // --- Stamp members; drop voice decls --------------------------------------
  std::unordered_set<std::string> decl_names;  // pre-expansion, for split_endpoint
  for (const auto& m : ast.modules) decl_names.insert(m.name);
  auto is_decl = [&](const std::string& n) { return decl_names.count(n) > 0; };

  std::vector<dsl::ModuleDecl> new_modules;
  for (const auto& m : ast.modules) {
    if (m.type == "voice") continue;  // group decl removed
    auto mg = member_group.find(m.name);
    if (mg == member_group.end()) { new_modules.push_back(m); continue; }
    int n = groups[mg->second].n;
    for (int i = 0; i < n; ++i) {
      dsl::ModuleDecl copy = m;
      copy.name = m.name + "#" + std::to_string(i);
      new_modules.push_back(std::move(copy));
    }
  }

  // --- Rewrite cables -------------------------------------------------------
  std::vector<dsl::CableDecl> new_cables;
  auto group_of = [&](const SplitEndpoint& s) -> std::string {
    if (!s.ok) return "";
    auto it = member_group.find(s.module);
    return it == member_group.end() ? "" : it->second;
  };

  for (const auto& c : ast.cables) {
    SplitEndpoint s =
        c.src.is_literal ? SplitEndpoint{} : split_endpoint(is_decl, c.src.module);
    SplitEndpoint d = split_endpoint(is_decl, c.dst.module);
    std::string sgroup = c.src.is_literal ? std::string{} : group_of(s);
    std::string dgroup = group_of(d);

    if (!dgroup.empty()) {
      const Group& dg = groups[dgroup];
      if (!sgroup.empty()) {
        const Group& sg = groups[sgroup];
        if (sgroup == dgroup) {
          // Case 1: inside -> inside, instance-local.
          for (int i = 0; i < dg.n; ++i) {
            dsl::CableDecl nc = c;
            nc.src.module = inst_chain(s.module, i, s.port);
            nc.dst.module = inst_chain(d.module, i, d.port);
            new_cables.push_back(std::move(nc));
          }
        } else {
          // Member link across two groups: n_src x n_dst.
          for (int i = 0; i < sg.n; ++i)
            for (int j = 0; j < dg.n; ++j) {
              dsl::CableDecl nc = c;
              nc.src.module = inst_chain(s.module, i, s.port);
              nc.dst.module = inst_chain(d.module, j, d.port);
              new_cables.push_back(std::move(nc));
            }
        }
      } else if (!c.src.is_literal && s.ok && s.module == dg.bound && dg.stems.count(s.port)) {
        // Case 2: bound module's bare bank name read into a member.
        // Bank ports are 1-based; instance i binds to port index i+1.
        for (int i = 0; i < dg.n; ++i) {
          dsl::CableDecl nc = c;
          nc.src.module = dg.bound + "." + s.port + std::to_string(i + 1);
          nc.dst.module = inst_chain(d.module, i, d.port);
          new_cables.push_back(std::move(nc));
        }
      } else {
        // Case 3: outside (or literal) -> inside, broadcast to every instance.
        for (int i = 0; i < dg.n; ++i) {
          dsl::CableDecl nc = c;  // src untouched
          nc.dst.module = inst_chain(d.module, i, d.port);
          new_cables.push_back(std::move(nc));
        }
      }
    } else if (!sgroup.empty()) {
      // Case 4: inside -> outside, straight sum of all instances.
      const Group& sg = groups[sgroup];
      for (int i = 0; i < sg.n; ++i) {
        dsl::CableDecl nc = c;  // dst untouched
        nc.src.module = inst_chain(s.module, i, s.port);
        new_cables.push_back(std::move(nc));
      }
    } else {
      new_cables.push_back(c);  // ordinary cable, unchanged
    }
  }

  ast.modules = std::move(new_modules);
  ast.cables = std::move(new_cables);
}

}  // namespace ms::graph
