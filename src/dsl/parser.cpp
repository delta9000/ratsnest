#include "dsl/parser.h"
#include <unordered_set>
#include "dsl/lexer.h"

namespace ms::dsl {
namespace {

class Parser {
 public:
  Parser(std::vector<Token> toks, Diags lex_errors)
      : toks_(std::move(toks)) { r_.errors = std::move(lex_errors); }

  ParseResult run() {
    std::unordered_set<std::string> names;
    while (!at(Tok::End)) {
      if (at(Tok::Newline)) { next(); continue; }
      if (at(Tok::Number)) { cable(); continue; }
      if (at(Tok::Ident)) {
        if (peek(1).kind == Tok::Equals) {
          auto m = decl();
          if (m && !names.insert(m->name).second)
            err(m->line, m->col, "duplicate module name '" + m->name + "'");
          continue;
        }
        cable();
        continue;
      }
      err(cur().line, cur().col, "expected a module declaration or cable");
      sync();
    }
    return std::move(r_);
  }

 private:
  const Token& cur() const { return toks_[i_]; }
  const Token& peek(size_t n) const {
    return toks_[std::min(i_ + n, toks_.size() - 1)];
  }
  bool at(Tok k) const { return cur().kind == k; }
  const Token& next() { const Token& t = cur(); if (!at(Tok::End)) ++i_; return t; }
  void err(int l, int c, std::string msg) { r_.errors.push_back({l, c, std::move(msg)}); }
  void sync() { while (!at(Tok::End) && !at(Tok::Newline)) next(); }
  bool expect(Tok k, const char* what) {
    if (at(k)) { next(); return true; }
    err(cur().line, cur().col, std::string("expected ") + what);
    sync();
    return false;
  }

  ModuleDecl* decl() {
    ModuleDecl m;
    const Token& name = next();
    m.name = name.text; m.line = name.line; m.col = name.col;
    const Token& eq = next();  // '='
    if (!at(Tok::Ident)) {
      err(eq.line, eq.col, "expected module type after '='");
      sync();
      return nullptr;
    }
    m.type = next().text;
    if (at(Tok::LBrace)) {
      next();
      while (!at(Tok::RBrace) && !at(Tok::End)) {
        ParamDecl p;
        const Token& pn = cur();
        if (!at(Tok::Ident)) { err(pn.line, pn.col, "expected parameter name"); sync(); return nullptr; }
        p.name = next().text; p.line = pn.line; p.col = pn.col;
        if (!expect(Tok::Colon, "':' after parameter name")) return nullptr;
        if (!value(p.value)) return nullptr;
        m.params.push_back(std::move(p));
        if (at(Tok::Comma)) next();
        else break;
      }
      if (!expect(Tok::RBrace, "'}' to close parameter block")) return nullptr;
    }
    r_.ast.modules.push_back(std::move(m));
    return &r_.ast.modules.back();
  }

  bool value(Value& v) {
    if (at(Tok::Number)) { v.kind = Value::Kind::Num; v.num = next().num; return true; }
    if (at(Tok::Ident))  { v.kind = Value::Kind::Ident; v.str = next().text; return true; }
    if (at(Tok::Str))    { v.kind = Value::Kind::Str; v.str = next().text; return true; }
    if (at(Tok::LBracket)) {
      next();
      v.kind = Value::Kind::Array;
      while (!at(Tok::RBracket) && !at(Tok::End)) {
        Value item;
        if (!value(item)) return false;
        v.items.push_back(std::move(item));
        if (at(Tok::Comma)) next(); else break;
      }
      return expect(Tok::RBracket, "']' to close array");
    }
    err(cur().line, cur().col, "expected a value (number, name, string, or array)");
    sync();
    return false;
  }

  void cable() {
    CableDecl cbl;
    const Token& first = cur();
    cbl.line = first.line; cbl.col = first.col;
    if (at(Tok::Number)) {
      cbl.src.is_literal = true;
      cbl.src.literal = next().num;
      cbl.src.line = first.line; cbl.src.col = first.col;
    } else if (!endpoint(cbl.src)) { return; }
    const Token& arrow = cur();
    if (!expect(Tok::Arrow, "'->'")) return;
    if (!at(Tok::Ident)) {
      err(arrow.line, arrow.col, "expected destination module after '->'");
      sync();
      return;
    }
    if (!endpoint(cbl.dst)) return;
    if (at(Tok::Colon)) {
      next();
      if (!at(Tok::Number)) {
        err(cur().line, cur().col, "expected a number for cable depth after ':'");
        sync();
        return;
      }
      cbl.depth = next().num;
    }
    r_.ast.cables.push_back(std::move(cbl));
  }

  bool endpoint(Endpoint& e) {
    const Token& m = next();
    e.module = m.text; e.line = m.line; e.col = m.col;
    if (at(Tok::Dot)) {
      next();
      if (!at(Tok::Ident)) {
        err(cur().line, cur().col, "expected port name after '.'");
        sync();
        return false;
      }
      e.port = next().text;
    }
    return true;
  }

  std::vector<Token> toks_;
  size_t i_ = 0;
  ParseResult r_;
};

}  // namespace

ParseResult parse(std::string_view src) {
  auto lx = lex(src);
  return Parser(std::move(lx.tokens), std::move(lx.errors)).run();
}
}  // namespace ms::dsl
