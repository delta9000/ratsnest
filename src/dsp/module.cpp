#include "dsp/module.h"

namespace ms::dsp {

std::string cfg_str(const Config& c, std::string_view name, std::string_view def) {
  for (const auto& [k, v] : c)
    if (k == name) return v.str;
  return std::string(def);
}

int ModuleInfo::input_index(std::string_view name) const {
  for (size_t i = 0; i < inputs.size(); ++i)
    if (name == inputs[i].name) return static_cast<int>(i);
  return -1;
}

int ModuleInfo::output_index(std::string_view name) const {
  for (size_t i = 0; i < outputs.size(); ++i)
    if (name == outputs[i].name) return static_cast<int>(i);
  return -1;
}

const ConfigInfo* ModuleInfo::config(std::string_view name) const {
  for (const auto& c : configs)
    if (name == c.name) return &c;
  return nullptr;
}

}  // namespace ms::dsp
