#pragma once
#include <string>
#include <string_view>
#include <vector>

namespace ms::dsl {

// The candidate a misspelled `word` most likely meant: the nearest by edit
// distance when that distance is small for the word's length, or one that
// is a prefix of the other (`trigger` -> `trig`, `waveform` -> `wave`).
// Empty when nothing is close. Ties go to the earliest candidate, so the
// answer is deterministic for a registry-ordered list.
std::string closest_name(std::string_view word, const std::vector<std::string>& candidates);

// " (did you mean 'x'?)" for closest_name, or `fallback` when nothing is close.
std::string did_you_mean(std::string_view word, const std::vector<std::string>& candidates,
                         std::string fallback = {});

// "a, b, c" -- the name lists diagnostics quote.
std::string join(const std::vector<std::string>& names);

}  // namespace ms::dsl
