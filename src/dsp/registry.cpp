#include "dsp/modules.h"

namespace ms::dsp {
namespace {
const ModuleInfo* const kRegistry[] = {&vca_info, &out_info, &vco_info};
}

const ModuleInfo* find_module(std::string_view type) {
  for (const ModuleInfo* m : kRegistry)
    if (type == m->type) return m;
  return nullptr;
}

std::span<const ModuleInfo* const> all_modules() { return kRegistry; }
}  // namespace ms::dsp
