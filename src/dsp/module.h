#pragma once
#include <cstddef>
#include <cstdint>
#include <memory>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <vector>
#include "dsl/unit.h"

namespace ms::dsp {

// Discrete controls bypass parameter glides on reload/live edits. `unit` is
// what a literal written for this input must measure, if it carries a suffix
// (dsl/unit.h); the helpers below declare one.
struct InputInfo  { const char* name; float def; bool glide = true; Unit unit = Unit::None; };
constexpr InputInfo in_hz(const char* n, float d)   { return {n, d, true, Unit::Freq}; }
constexpr InputInfo in_secs(const char* n, float d) { return {n, d, true, Unit::Time}; }
constexpr InputInfo in_bpm(const char* n, float d)  { return {n, d, true, Unit::Tempo}; }
constexpr InputInfo in_voct(const char* n, float d) { return {n, d, true, Unit::Pitch}; }
enum class ConfigKind { Number, Text, Numbers, Enums, Texts };
struct ConfigInfo {
  const char* name;
  const char* const* enum_vals;
  ConfigKind kind = ConfigKind::Number;
  double def = 0.0;  // a Number config's default, where code outside the module needs it
};
struct OutputInfo { const char* name; };

struct ConfigValue {
  std::string str;
  double num = 0.0;
  std::vector<double> arr;
  bool is_num = false;
  // Default member initializer so existing positional ConfigValue{str,num,arr,
  // is_num} aggregates stay valid under -Werror=missing-field-initializers.
  std::vector<std::string> strs{};
};
using Config = std::vector<std::pair<std::string, ConfigValue>>;

std::string cfg_str(const Config& c, std::string_view name, std::string_view def);
double cfg_num(const Config& c, std::string_view name, double def);
const std::vector<double>& cfg_arr(const Config& c, std::string_view name);
const std::vector<std::string>& cfg_str_arr(const Config& c, std::string_view name);
// Position of `s` in a nullptr-terminated enum list, or `def` if absent.
int enum_index(const char* const* vals, std::string_view s, int def = 0);
// The named config's position in `vals`; `def` when unset or unknown.
int cfg_enum(const Config& c, std::string_view name, const char* const* vals, int def = 0);

// Raw MIDI event as delivered to on_midi: up to 4 status/data bytes (running
// status is expanded by the producer, so len is 1..3 for channel voice
// messages), trivially copyable so it can live in an rt::SpscQueue.
struct MidiEvent {
  uint8_t data[4];
  uint8_t len;
};

class Module {
 public:
  virtual ~Module() = default;
  virtual void init(double sample_rate, const Config& cfg) { (void)sample_rate; (void)cfg; }
  virtual void process(const float* ins, float* outs) = 0;
  // Copy migratable runtime state from another instance of the SAME concrete
  // type (caller guarantees it). RT-called: no alloc, no locks, no I/O.
  virtual void copy_state_from(const Module& other) { (void)other; }
  // Delivered once per queued MIDI event, at the start of the block that
  // contains it (events quantize to block boundaries; see
  // docs/superpowers/specs/2026-07-11-midi-design.md §3). Only called on
  // modules whose ModuleInfo::wants_midi is set. RT-called, ahead of that
  // block's process() calls: no alloc, no locks, no I/O. `data` points into
  // caller-owned storage valid only for the duration of the call.
  virtual void on_midi(const uint8_t* data, size_t len) { (void)data; (void)len; }
  // Delivered at block start, before that block's process() calls, carrying the
  // audio samples captured since the previous block. Only called on modules
  // whose ModuleInfo::wants_audio_in is set. RT-called: no alloc, no locks, no
  // I/O. `samples` points into caller-owned storage valid only for this call.
  virtual void on_audio_in(const float* samples, size_t n) { (void)samples; (void)n; }
};

struct ModuleInfo {
  const char* type;
  std::span<const InputInfo> inputs;
  std::span<const ConfigInfo> configs;
  std::span<const OutputInfo> outputs;
  std::unique_ptr<Module> (*create)();
  // Opts into on_midi delivery; the runner fans queued MIDI events to every
  // node with wants_midi set, at block start, before that block is processed.
  bool wants_midi = false;
  // Opts into on_audio_in delivery; the runner fans captured external audio to
  // every node with wants_audio_in set, at block start, before processing.
  bool wants_audio_in = false;
  // Optional graph-compile-time config validation. Returns "" to accept,
  // else the message reported at the declaration's line:col. The controller
  // discards graphs with validation errors, so compiled graphs only carry
  // validated configs; init() still clamps defensively (tests hand it raw
  // configs directly).
  std::string (*validate)(const Config&) = nullptr;

  int input_index(std::string_view name) const;
  int output_index(std::string_view name) const;
  const ConfigInfo* config(std::string_view name) const;
};

const ModuleInfo* find_module(std::string_view type);
std::span<const ModuleInfo* const> all_modules();

}  // namespace ms::dsp
