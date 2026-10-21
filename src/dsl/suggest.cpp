#include "dsl/suggest.h"
#include <algorithm>
#include <numeric>

namespace ms::dsl {
namespace {

// Levenshtein distance, one rolling row.
size_t edit_distance(std::string_view a, std::string_view b) {
  std::vector<size_t> row(b.size() + 1);
  std::iota(row.begin(), row.end(), size_t{0});
  for (size_t i = 1; i <= a.size(); ++i) {
    size_t diag = row[0];
    row[0] = i;
    for (size_t j = 1; j <= b.size(); ++j) {
      const size_t up = row[j];
      row[j] = std::min({row[j] + 1, row[j - 1] + 1, diag + (a[i - 1] != b[j - 1])});
      diag = up;
    }
  }
  return row[b.size()];
}

// One name starts the other, and the shorter is a real word, not a letter.
bool prefix_related(std::string_view a, std::string_view b) {
  if (a.size() > b.size()) std::swap(a, b);
  return a.size() >= 2 && b.starts_with(a);
}

}  // namespace

std::string closest_name(std::string_view word, const std::vector<std::string>& candidates) {
  const size_t limit = (word.size() + 1) / 3;  // one edit per three letters
  std::string best;
  size_t best_d = limit + 1;
  for (const auto& c : candidates) {
    const size_t d = prefix_related(word, c) ? limit : edit_distance(word, c);
    if (d < best_d) { best_d = d; best = c; }
  }
  return best;
}

std::string did_you_mean(std::string_view word, const std::vector<std::string>& candidates,
                         std::string fallback) {
  const std::string best = closest_name(word, candidates);
  return best.empty() ? fallback : " (did you mean '" + best + "'?)";
}

std::string join(const std::vector<std::string>& names) {
  std::string s;
  for (const auto& n : names) s += ", " + n;
  return s.substr(std::min<size_t>(2, s.size()));
}

}  // namespace ms::dsl
