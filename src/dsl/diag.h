#pragma once
#include <string>
#include <vector>

namespace ms {
struct Diag {
  int line = 0;
  int col = 0;
  std::string msg;
  std::string file;
};
using Diags = std::vector<Diag>;
}  // namespace ms
