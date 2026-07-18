#pragma once
#include <string>
#include <vector>

namespace ms::dsl {

struct Value {
  enum class Kind { Num, Ident, Str, Array };
  Kind kind = Kind::Num;
  double num = 0.0;
  std::string str;
  std::vector<Value> items;
};

struct ParamDecl { std::string name; Value value; int line = 0, col = 0; };

struct ModuleDecl {
  std::string name, type;
  std::vector<ParamDecl> params;
  int line = 0, col = 0;
};

struct Endpoint {
  bool is_literal = false;
  double literal = 0.0;
  std::string module, port;  // port empty = default port
  int line = 0, col = 0;
};

struct CableDecl {
  Endpoint src, dst;
  double depth = 1.0;
  std::string depth_ident;
  int line = 0, col = 0;
};

struct PatchAst {
  std::vector<ModuleDecl> modules;
  std::vector<CableDecl> cables;
};
}  // namespace ms::dsl
