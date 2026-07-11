#pragma once
#include <memory>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace ms::dsp {

struct InputInfo  { const char* name; float def; };
struct ConfigInfo { const char* name; const char* const* enum_vals; };
struct OutputInfo { const char* name; };

struct ConfigValue {
  std::string str;
  double num = 0.0;
  std::vector<double> arr;
  bool is_num = false;
};
using Config = std::vector<std::pair<std::string, ConfigValue>>;

std::string cfg_str(const Config& c, std::string_view name, std::string_view def);
double cfg_num(const Config& c, std::string_view name, double def);
const std::vector<double>& cfg_arr(const Config& c, std::string_view name);

class Module {
 public:
  virtual ~Module() = default;
  virtual void init(double sample_rate, const Config& cfg) { (void)sample_rate; (void)cfg; }
  virtual void process(const float* ins, float* outs) = 0;
  // Copy migratable runtime state from another instance of the SAME concrete
  // type (caller guarantees it). RT-called: no alloc, no locks, no I/O.
  virtual void copy_state_from(const Module& other) { (void)other; }
};

struct ModuleInfo {
  const char* type;
  std::span<const InputInfo> inputs;
  std::span<const ConfigInfo> configs;
  std::span<const OutputInfo> outputs;
  std::unique_ptr<Module> (*create)();

  int input_index(std::string_view name) const;
  int output_index(std::string_view name) const;
  const ConfigInfo* config(std::string_view name) const;
};

const ModuleInfo* find_module(std::string_view type);
std::span<const ModuleInfo* const> all_modules();

}  // namespace ms::dsp
