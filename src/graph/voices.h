#pragma once
#include <string>
#include "dsl/ast.h"
#include "dsl/diag.h"

namespace ms::graph {

// The result of splitting a dotted endpoint chain into a declared module name
// and (at most one) trailing port segment.
struct SplitEndpoint {
  std::string module;
  std::string port;  // empty = default port
  bool ok = false;
};

// Split a dotted chain into (declared module name, port). The longest declared
// name equal to the chain, or a dot-prefix of it, wins; the remainder (at most
// one trailing segment) is the port. e.g. with `lead.osc` declared,
// `lead.osc.v_oct` resolves to module `lead.osc`, port `v_oct`.
//
// This is the SINGLE implementation of Task 1's resolution rule, shared by the
// voice-expansion pass and graph::compile. `is_declared(name)` answers whether
// a name is a declared module (a set/map lookup at the caller's discretion).
template <class IsDeclared>
SplitEndpoint split_endpoint(IsDeclared&& is_declared, const std::string& chain) {
  if (is_declared(chain)) return {chain, "", true};
  auto pos = chain.rfind('.');
  if (pos != std::string::npos) {
    std::string head = chain.substr(0, pos);
    if (is_declared(head)) return {head, chain.substr(pos + 1), true};
  }
  return {};
}

// Compile-time voice-group expansion (spec §3). Removes `voice` decls, stamps
// each member module once per voice as `<member>#<i>` (0-based), and rewrites
// cables per the four resolution cases (instance-local, per-instance bank read,
// broadcast, straight sum; plus n1*n2 for member links across two groups).
// Pure AST->AST with deterministic order (member decl order x ascending i) so
// hot reload's same_structure holds. Diagnostics (line:col) are appended to
// `errors`; when any error is produced the AST is left unmodified.
void expand_voices(dsl::PatchAst& ast, Diags& errors);

}  // namespace ms::graph
