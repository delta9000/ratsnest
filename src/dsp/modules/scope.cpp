#include <algorithm>
#include <cmath>
#include <string>
#include "dsp/modules.h"

namespace ms::dsp {
namespace {

constexpr InputInfo kIns[] = {{"in", 0.f}};
constexpr ConfigInfo kCfg[] = {{"label", nullptr}};

class Scope final : public Module {
 public:
  void init(double, const Config& cfg) override {
    label_ = cfg_str(cfg, "label", "");
    peak_ = 0.0f;
    last_ = 0.0f;
  }

  void process(const float* ins, float*) override {
    last_ = ins[0];
    peak_ = std::max(peak_ * 0.9995f, std::abs(ins[0]));
  }

  void copy_state_from(const Module& other) override {
    const auto& o = static_cast<const Scope&>(other);
    peak_ = o.peak_;
    last_ = o.last_;
  }

 private:
  std::string label_;  // set in init (non-RT); read by the future TUI
  float peak_ = 0.0f;
  float last_ = 0.0f;
};

std::unique_ptr<Module> make() { return std::make_unique<Scope>(); }
}  // namespace

const ModuleInfo scope_info{"scope", kIns, kCfg, {}, &make};
}  // namespace ms::dsp
