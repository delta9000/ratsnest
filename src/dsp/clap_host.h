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
// `value` is what the fresh instance holds (get_value), which is what an
// unlisted param plays at; some plugins (Surge XT) report every
// `def` as 0, so `value` is the number to trust.
struct Param {
  clap_id id;
  std::string name, module;
  double min, max, def, value;
  bool stepped;
};
std::vector<Param> params(const clap_plugin_t* p);

// The plugin's display text for `v` ("800.00 Hz", "Wavetable"); "" if it
// has none.
std::string text(const clap_plugin_t* p, clap_id id, double v);

// The param named `name` (or with that numeric id); nullptr if none.
const Param* find(const std::vector<Param>& all, const std::string& name);

// A `set:` value as a plain param value: the plugin's own parse of the text
// (as its display writes it), else a number in the param's range.
bool parse(const clap_plugin_t* p, const Param& q, const std::string& s, double& v);

// `set: ["A Osc 1 Type=Wavetable"]` on an initialised, inactive instance:
// each entry parsed and flushed in order, re-reading the params after each,
// since a plugin may rename params when another changes (Surge's "FX A1
// Param 3" reads "FX A1 Reverb - Decay Time" once "FX A1 FX Type" is
// Reverb 2). "" on success, else what was wrong.
std::string apply(const clap_plugin_t* p, const std::vector<std::string>& sets);

}  // namespace ms::dsp::clap
