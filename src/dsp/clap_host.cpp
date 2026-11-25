#include "dsp/clap_host.h"

#include <dlfcn.h>

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <map>
#include <mutex>
#include <sstream>

namespace ms::dsp::clap {
namespace {

namespace fs = std::filesystem;

std::string home() {
  const char* h = std::getenv("HOME");
  return h ? h : ".";
}

std::vector<fs::path> search_path() {
  std::vector<fs::path> dirs;
  if (const char* env = std::getenv("CLAP_PATH")) {
    std::stringstream ss(env);
    for (std::string d; std::getline(ss, d, ':');)
      if (!d.empty()) dirs.emplace_back(d);
  }
  for (const char* d : {"/.clap", "/usr/lib/clap", "/usr/local/lib/clap"})
    dirs.emplace_back(d[1] == '.' ? home() + d : std::string(d));
  return dirs;
}

struct Lib {
  const clap_plugin_factory_t* factory = nullptr;
  std::string error;
};

Lib open_lib(const std::string& path) {
  Lib lib;
  void* h = dlopen(path.c_str(), RTLD_NOW | RTLD_LOCAL);
  const auto* entry = h ? static_cast<const clap_plugin_entry_t*>(dlsym(h, "clap_entry")) : nullptr;
  if (!entry) {
    lib.error = h ? "no clap_entry in " + path : std::string(dlerror());
    return lib;
  }
  if (!clap_version_is_compatible(entry->clap_version) || !entry->init(path.c_str())) {
    lib.error = "cannot initialise " + path;
    return lib;
  }
  lib.factory = static_cast<const clap_plugin_factory_t*>(entry->get_factory(CLAP_PLUGIN_FACTORY_ID));
  if (!lib.factory) lib.error = "no plugin factory in " + path;
  return lib;
}

void h_log(const clap_host_t*, clap_log_severity sev, const char* msg) {
  if (sev >= CLAP_LOG_WARNING) std::fprintf(stderr, "plugin: %s\n", msg);
}
const clap_host_log_t kLog{h_log};
const void* h_ext(const clap_host_t*, const char* id) {
  return std::string_view(id) == CLAP_EXT_LOG ? &kLog : nullptr;
}
void h_noop(const clap_host_t*) {}
const clap_host_t kHost{CLAP_VERSION_INIT, nullptr, "ratsnest", "ratsnest", "", "0.1.0",
                        h_ext, h_noop, h_noop, h_noop};

}  // namespace

std::string resolve(const std::string& file) {
  if (file.empty()) return {};
  std::error_code ec;
  if (file.find('/') != std::string::npos) {
    const std::string p = file.rfind("~/", 0) == 0 ? home() + file.substr(1) : file;
    return fs::exists(p, ec) ? p : std::string();
  }
  const std::string name = file.ends_with(".clap") ? file : file + ".clap";
  for (const auto& d : search_path())
    if (fs::exists(d / name, ec)) return (d / name).string();
  return {};
}

std::vector<std::string> installed() {
  std::vector<std::string> found;
  std::error_code ec;
  for (const auto& d : search_path())
    for (const auto& e : fs::directory_iterator(d, ec))
      if (e.path().extension() == ".clap") found.push_back(e.path().string());
  std::sort(found.begin(), found.end());
  return found;
}

const clap_plugin_factory_t* factory(const std::string& path, std::string& error) {
  static std::mutex m;
  static std::map<std::string, Lib> libs;
  const std::lock_guard<std::mutex> lock(m);
  auto it = libs.find(path);
  if (it == libs.end()) it = libs.emplace(path, open_lib(path)).first;
  error = it->second.error;
  return it->second.factory;
}

const clap_host_t* host() { return &kHost; }

namespace {
const clap_plugin_params_t* params_ext(const clap_plugin_t* p) {
  return static_cast<const clap_plugin_params_t*>(p->get_extension(p, CLAP_EXT_PARAMS));
}
}  // namespace

std::vector<Param> params(const clap_plugin_t* p) {
  std::vector<Param> out;
  const auto* ext = params_ext(p);
  for (uint32_t i = 0, n = ext ? ext->count(p) : 0; i < n; ++i) {
    clap_param_info_t info{};
    if (!ext->get_info(p, i, &info)) continue;
    double v = info.default_value;
    if (!ext->get_value(p, info.id, &v)) v = info.default_value;
    out.push_back({info.id, info.name, info.module, info.min_value, info.max_value,
                   info.default_value, v, (info.flags & CLAP_PARAM_IS_STEPPED) != 0});
  }
  return out;
}

std::string text(const clap_plugin_t* p, clap_id id, double v) {
  const auto* ext = params_ext(p);
  char buf[256] = {};
  return ext && ext->value_to_text(p, id, v, buf, sizeof buf) ? std::string(buf) : std::string();
}

bool parse(const clap_plugin_t* p, const Param& q, const std::string& s, double& v) {
  const auto* ext = params_ext(p);
  if (ext && ext->text_to_value(p, q.id, s.c_str(), &v)) return true;
  char* end = nullptr;
  v = std::strtod(s.c_str(), &end);
  return end != s.c_str() && *end == '\0' && v >= q.min && v <= q.max;
}

const Param* find(const std::vector<Param>& all, const std::string& name) {
  const auto it = std::find_if(all.begin(), all.end(), [&](const Param& q) {
    return q.name == name || std::to_string(q.id) == name;
  });
  return it == all.end() ? nullptr : &*it;
}

namespace {
struct OneEvent {
  clap_event_param_value_t ev;
  static uint32_t size(const clap_input_events_t*) { return 1; }
  static const clap_event_header_t* get(const clap_input_events_t* l, uint32_t) {
    return &static_cast<const OneEvent*>(l->ctx)->ev.header;
  }
  static bool push(const clap_output_events_t*, const clap_event_header_t*) { return true; }
};
}  // namespace

std::string apply(const clap_plugin_t* p, const std::vector<std::string>& sets) {
  const auto* ext = params_ext(p);
  auto all = params(p);
  for (const std::string& s : sets) {
    const size_t eq = s.find('=');
    const std::string name = s.substr(0, eq);
    const Param* q = eq == std::string::npos ? nullptr : find(all, name);
    if (!q) return "set: no param named '" + name + "' (write \"Name=value\")";
    double v = 0.0;
    if (!parse(p, *q, s.substr(eq + 1), v))
      return "set: '" + name + "' cannot take '" + s.substr(eq + 1) + "'";
    OneEvent one{{{sizeof(clap_event_param_value_t), 0, CLAP_CORE_EVENT_SPACE_ID,
                   CLAP_EVENT_PARAM_VALUE, 0}, q->id, nullptr, -1, -1, -1, -1, v}};
    const clap_input_events_t in{&one, &OneEvent::size, &OneEvent::get};
    const clap_output_events_t out{nullptr, &OneEvent::push};
    ext->flush(p, &in, &out);
    all = params(p);
  }
  return {};
}

}  // namespace ms::dsp::clap
