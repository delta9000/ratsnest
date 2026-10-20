#include "dsp/mastering.h"
#include "dsp/modules.h"
#include "dsp/util.h"
#include <algorithm>
#include <cmath>
#include <utility>
#include <vector>

namespace ms::dsp {
namespace {
constexpr InputInfo kIns[] = {{"clock", 0.f, false},
                              {"request", -1.f, false},
                              {"launch", 0.f, false},
                              {"reset", 0.f, false},
                              {"hold", 0.f, false}};
constexpr ConfigInfo kCfg[] = {{"bars", nullptr, ConfigKind::Numbers}};
constexpr OutputInfo kOuts[] = {{"index"}, {"trig"}, {"bar"}, {"pending"}};
std::string validate(const Config &cfg) {
  const auto &v = cfg_arr(cfg, "bars");
  if (v.empty() || v.size() > 64)
    return "bars needs 1..64 scene lengths";
  for (double x : v)
    if (!std::isfinite(x) || x < 1 || x > 65536 || std::floor(x) != x)
      return "scene lengths must be integer bar counts in 1..65536";
  return {};
}
class Scene final : public Module {
public:
  void init(double, const Config &cfg) override {
    bars_ = cfg_arr(cfg, "bars");
    if (bars_.empty())
      bars_ = {4};
  }
  void process(const float *in, float *out) override {
    const bool clock = in[0] > .5f, launch = in[2] > .5f, reset = in[3] > .5f;
    const bool edge = clock && !clock_;
    const int request = static_cast<int>(std::round(
        sanitize_clamp(in[1], -1, static_cast<float>(bars_.size() - 1), -1)));
    if (reset && !reset_) {
      index_ = -1;
      bar_ = 0;
      pending_ = -1;
    }
    queue(request, launch && !launch_);
    clock_ = clock;
    launch_ = launch;
    reset_ = reset;
    index_ = std::min(index_, static_cast<int>(bars_.size()) - 1);
    pending_ = std::min(pending_, static_cast<int>(bars_.size()) - 1);
    const bool changed = edge && next_bar(in[4] <= .5f);
    out[0] = static_cast<float>(std::max(0, index_));
    out[1] = as_gate(changed);
    out[2] = static_cast<float>(bar_);
    out[3] = static_cast<float>(pending_);
  }
  void copy_state_from(const Module &other) override {
    const auto &o = static_cast<const Scene &>(other);
    index_ = o.index_;
    bar_ = o.bar_;
    pending_ = o.pending_;
    request_ = o.request_;
    clock_ = o.clock_;
    launch_ = o.launch_;
    reset_ = o.reset_;
  }

private:
  // Changed addresses queue once; launch can explicitly restart the same
  // scene. Moving the address to "none" cancels the queue.
  void queue(int request, bool launched) {
    if (request >= 0 && (request != request_ || launched))
      pending_ = request;
    if (request < 0 && request != request_)
      pending_ = -1;
    request_ = request;
  }
  // One bar clock: start a queued scene, start scene 0 from idle, or count a
  // bar and move on at the end of the scene unless held. Returns whether the
  // scene changed.
  bool next_bar(bool unheld) {
    if (pending_ >= 0) {
      index_ = std::exchange(pending_, -1);
      bar_ = 0;
      return true;
    }
    if (index_ < 0) {
      index_ = 0;
      bar_ = 0;
      return true;
    }
    if (++bar_ < static_cast<int>(bars_[static_cast<size_t>(index_)]))
      return false;
    bar_ = 0;
    if (!unheld)
      return false;
    index_ = (index_ + 1) % static_cast<int>(bars_.size());
    return true;
  }

  std::vector<double> bars_;
  int index_ = -1, bar_ = 0, pending_ = -1, request_ = -1;
  bool clock_ = false, launch_ = false, reset_ = false;
};
std::unique_ptr<Module> make() { return std::make_unique<Scene>(); }
} // namespace
const ModuleInfo scene_info{"scene", kIns,  kCfg,  kOuts,
                            &make,   false, false, &validate};
} // namespace ms::dsp
