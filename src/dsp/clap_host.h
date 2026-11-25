#pragma once
// Loading third-party CLAP plugins (docs/plugin.md, "Loading plugins").
// Non-RT only: everything here may allocate and touch the filesystem.
#include <clap/clap.h>

#include <string>
#include <vector>

namespace ms::dsp::clap {

// A `file:` value as a .clap path: a path (with `~/`) is taken as is; a bare
// name ("Surge XT") is looked up as <name>.clap in $CLAP_PATH, ~/.clap,
// /usr/lib/clap and /usr/local/lib/clap. "" when nothing is found.
std::string resolve(const std::string& file);

// Every .clap file on the search path, sorted.
std::vector<std::string> installed();

// The library's factory, loading and initialising it on first use. Libraries
// stay loaded for the life of the process (many plugins misbehave when
// unloaded); a failed load is remembered too. nullptr on failure, with
// `error` saying why.
const clap_plugin_factory_t* factory(const std::string& path, std::string& error);

// The host every loaded plugin sees: no GUI, no threads, logs to stderr.
const clap_host_t* host();

// A plugin's params, read from an initialised (not yet activated) instance.
struct Param {
  clap_id id;
  std::string name, module;
  double min, max, def;
  bool stepped;
};
std::vector<Param> params(const clap_plugin_t* p);

}  // namespace ms::dsp::clap
