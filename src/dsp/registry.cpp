#include "dsp/modules.h"

namespace ms::dsp {
namespace {
const ModuleInfo* const kRegistry[] = {
    &vca_info, &out_info, &vco_info, &lfo_info, &clock_info, &div_info, &func_info,
    &svf_info, &noise_info, &math_info, &xfade_info, &drive_info};
}

const ModuleInfo* find_module(std::string_view type) {
  for (const ModuleInfo* m : kRegistry)
    if (type == m->type) return m;
  return nullptr;
}

std::span<const ModuleInfo* const> all_modules() { return kRegistry; }
}  // namespace ms::dsp
