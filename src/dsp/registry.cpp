#include "dsp/modules.h"

namespace ms::dsp {
namespace {
const ModuleInfo* const kRegistry[] = {
    &vca_info, &out_info, &vco_info, &lfo_info, &clock_info, &div_info, &func_info,
    &svf_info, &noise_info, &math_info, &xfade_info, &drive_info, &fold_info,
    &sh_info, &ladder_info, &lpg_info, &random_info, &quant_info, &mix_info,
    &scope_info, &delay_info, &adsr_info, &reverb_info, &euclid_info,
    &bern_info, &logic_info, &switch_info, &turing_info, &stepseq_info,
    &tempo_info};
}

const ModuleInfo* find_module(std::string_view type) {
  for (const ModuleInfo* m : kRegistry)
    if (type == m->type) return m;
  return nullptr;
}

std::span<const ModuleInfo* const> all_modules() { return kRegistry; }
}  // namespace ms::dsp
