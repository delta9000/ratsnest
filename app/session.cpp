#include "session.h"
#include "dsl/lexer.h"
#include "dsl/parser.h"
#include <cctype>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <stdexcept>
#include <unistd.h>

namespace ms::studio {
namespace {
std::string read(const std::string &path) {
  std::ifstream f(path);
  if (!f)
    throw std::runtime_error("Cannot read " + path);
  return {std::istreambuf_iterator<char>(f), std::istreambuf_iterator<char>()};
}
std::string diagnostics(const Diags &errors) {
  std::string out;
  for (const auto &d : errors)
    out += d.file + ":" + std::to_string(d.line) + ":" + std::to_string(d.col) +
           ": " + d.msg + "\n";
  return out;
}
size_t offset(const std::string &text, int line, int col) {
  size_t at = 0;
  for (int i = 1; i < line; ++i) {
    at = text.find('\n', at);
    if (at == std::string::npos)
      throw std::runtime_error("Invalid source location");
    ++at;
  }
  return at + static_cast<size_t>(col - 1);
}
size_t token_end(const std::string &source, const dsl::Token &token) {
  size_t end = offset(source, token.line, token.col);
  if (token.kind == dsl::Tok::Str)
    return end + token.text.size() + 2;
  if (token.kind != dsl::Tok::Number)
    return end + (token.text.empty() ? 1 : token.text.size());
  while (end < source.size() &&
         !std::isspace(static_cast<unsigned char>(source[end])) &&
         source[end] != ',' && source[end] != ']' && source[end] != '}' &&
         source[end] != '#')
    ++end;
  return end;
}

} // namespace
Session::Session(std::string path, double rate)
    : path_(std::filesystem::canonical(path).string()), runner_(rate),
      controller_(runner_, rate) {
  disk_source_ = read(path_);
  auto result = apply(disk_source_, 0);
  (void)result;
  engine::Command observe;
  observe.kind = engine::Command::Kind::Observe;
  observe.value = 1;
  runner_.push(observe);
}
void Session::require_revision(uint64_t expected) const {
  if (expected != revision_)
    throw std::runtime_error(
        "Revision conflict: read state again before editing (current " +
        std::to_string(revision_) + ")");
}
Json Session::apply(const std::string &text, uint64_t expected, bool record) {
  require_revision(expected);
  if (text.size() > 4 * 1024 * 1024)
    throw std::runtime_error("Patch exceeds 4 MiB");
  auto result = controller_.load(text, path_);
  if (!result.ok)
    throw std::runtime_error(diagnostics(result.errors));
  if (record && revision_ > 0 && source_ != text) {
    undo_.push_back(source_);
    if (undo_.size() > 32)
      undo_.erase(undo_.begin());
    redo_.clear();
  }
  source_ = text;
  ++revision_;
  message_ = "Patch applied";
  auto files = result.files;
  files.push_back(path_);
  watcher_ = std::make_unique<io::PatchWatcher>(files);
  return {{"revision", revision_}, {"dirty", dirty()}};
}
void Session::poll() {
  controller_.drain_retired();
  engine::Observation latest;
  while (runner_.try_observe(latest))
    if (latest.generation == controller_.generation())
      observation_ = latest;
  if (watcher_ && watcher_->poll_changed(0)) {
    try {
      const auto text = read(path_);
      if (dirty() && text != disk_source_) {
        message_ = "Disk changed while live edits are unsaved; save refused "
                   "until reconciled";
        return;
      }
      // Also reload when only a referenced subpatch changed.
      apply(dirty() ? source_ : text, revision_);
      disk_source_ = text;
      message_ = "Reloaded external edit";
    } catch (const std::exception &e) {
      message_ = e.what();
    }
  }
}
Json Session::save(uint64_t expected) {
  require_revision(expected);
  if (read(path_) != disk_source_)
    throw std::runtime_error(
        "Disk conflict: file changed externally; reconcile before saving");
  std::string pattern = path_ + ".studio-XXXXXX";
  std::vector<char> name(pattern.begin(), pattern.end());
  name.push_back(0);
  const int fd = mkstemp(name.data());
  if (fd < 0)
    throw std::runtime_error("Cannot create save file");
  const std::string temp = name.data();
  bool closed = false;
  try {
    size_t at = 0;
    while (at < source_.size()) {
      const auto n = write(fd, source_.data() + at, source_.size() - at);
      if (n < 0 && errno == EINTR)
        continue;
      if (n <= 0)
        throw std::runtime_error("Save failed");
      at += static_cast<size_t>(n);
    }
    std::filesystem::permissions(temp,
                                 std::filesystem::status(path_).permissions());
    if (fsync(fd) < 0)
      throw std::runtime_error("Cannot flush save file");
    close(fd);
    closed = true;
    if (read(path_) != disk_source_)
      throw std::runtime_error("Disk conflict during save");
    std::filesystem::rename(temp, path_);
  } catch (...) {
    if (!closed)
      close(fd);
    unlink(temp.c_str());
    throw;
  }
  disk_source_ = source_;
  message_ = "Saved " + path_;
  return {{"revision", revision_}, {"dirty", false}};
}
Json Session::set_param(const Json &args) {
  require_revision(args.at("revision").get<uint64_t>());
  const auto name = args.at("module").get<std::string>(),
             param = args.at("parameter").get<std::string>();
  const auto value = args.at("value").get<std::string>();
  auto identifier = dsl::lex(param);
  if (identifier.tokens.size() != 2 ||
      identifier.tokens[0].kind != dsl::Tok::Ident ||
      identifier.tokens[0].text != param)
    throw std::runtime_error("parameter must be a single identifier");
  // Validate the replacement as a single DSL value, never arbitrary appended
  // code.
  auto probe = dsl::parse("probe = math { b: " + value + " }");
  if (!probe.ok() || probe.ast.modules.size() != 1 ||
      probe.ast.modules[0].params.size() != 1 || !probe.ast.cables.empty())
    throw std::runtime_error("value must contain exactly one DSL value");
  auto parsed = dsl::parse(source_);
  auto lexed = dsl::lex(source_);
  for (const auto &module : parsed.ast.modules)
    if (module.name == name) {
      size_t at = offset(source_, module.line, module.col);
      const auto &tokens = lexed.tokens;
      size_t begin = 0;
      while (begin < tokens.size() &&
             (tokens[begin].kind == dsl::Tok::Newline ||
              offset(source_, tokens[begin].line, tokens[begin].col) < at))
        ++begin;
      // Find the declaration's closing brace or next line (bare declarations).
      size_t close = begin;
      while (close < tokens.size() && tokens[close].kind != dsl::Tok::RBrace &&
             tokens[close].kind != dsl::Tok::Newline &&
             tokens[close].kind != dsl::Tok::End)
        ++close;
      for (const auto &p : module.params)
        if (p.name == param) {
          const size_t loc = offset(source_, p.line, p.col);
          size_t t = begin;
          while (t < tokens.size() &&
                 offset(source_, tokens[t].line, tokens[t].col) != loc)
            ++t;
          t += 2;
          const size_t first = t;
          int brackets = 0;
          do {
            if (tokens[t].kind == dsl::Tok::LBracket)
              ++brackets;
            if (tokens[t].kind == dsl::Tok::RBracket)
              --brackets;
            ++t;
          } while (t < tokens.size() &&
                   (brackets > 0 || tokens[t].kind == dsl::Tok::Dot ||
                    tokens[t - 1].kind == dsl::Tok::Dot));
          const size_t start =
              offset(source_, tokens[first].line, tokens[first].col);
          const auto &last = tokens[t - 1];
          const size_t end = token_end(source_, last);
          std::string edited = source_;
          edited.replace(start, end - start, value);
          return apply(edited, revision_);
        }
      std::string edited = source_;
      const size_t end = offset(source_, tokens[close].line, tokens[close].col);
      if (tokens[close].kind == dsl::Tok::RBrace)
        edited.insert(end, (module.params.empty() ||
                                    tokens[close - 1].kind == dsl::Tok::Comma
                                ? ""
                                : ", ") +
                               param + ": " + value + " ");
      else {
        size_t type = begin;
        while (tokens[type].kind != dsl::Tok::Equals)
          ++type;
        ++type;
        edited.insert(offset(source_, tokens[type].line, tokens[type].col) +
                          tokens[type].text.size(),
                      " { " + param + ": " + value + " }");
      }
      return apply(edited, revision_);
    }
  throw std::runtime_error(
      "Parameter edits require an authored module; edit the parent subpatch or "
      "replace source for expanded nodes");
}
Json Session::remove(const Json &args, bool node) {
  require_revision(args.at("revision").get<uint64_t>());
  const auto ast = dsl::parse(source_).ast;
  std::vector<size_t> starts;
  for (const auto &m : ast.modules)
    starts.push_back(offset(source_, m.line, m.col));
  for (const auto &c : ast.cables)
    starts.push_back(offset(source_, c.line, c.col));
  starts.push_back(source_.size());
  std::sort(starts.begin(), starts.end());
  std::vector<std::pair<size_t, size_t>> removals;
  const auto tokens = dsl::lex(source_).tokens;
  auto erase = [&](int line, int col) {
    const size_t a = offset(source_, line, col);
    auto next = std::upper_bound(starts.begin(), starts.end(), a);
    const size_t end = next == starts.end() ? source_.size() : *next;
    size_t actual = a;
    for (const auto &token : tokens) {
      if (token.kind == dsl::Tok::Newline || token.kind == dsl::Tok::End)
        continue;
      const auto begin = offset(source_, token.line, token.col);
      if (begin >= a && begin < end)
        actual = token_end(source_, token);
    }
    removals.push_back({a, actual - a});
  };
  auto root = [&](const std::string &endpoint) {
    std::string best;
    for (const auto &m : ast.modules)
      if ((endpoint == m.name || endpoint.starts_with(m.name + ".")) &&
          m.name.size() > best.size())
        best = m.name;
    return best;
  };
  if (node) {
    const auto name = args.at("module").get<std::string>();
    bool found = false;
    for (const auto &m : ast.modules)
      if (m.name == name) {
        erase(m.line, m.col);
        found = true;
      }
    if (!found)
      throw std::runtime_error(
          "Cannot delete an expanded node; edit its parent subpatch");
    for (const auto &c : ast.cables)
      if ((!c.src.is_literal && root(c.src.module) == name) ||
          root(c.dst.module) == name)
        erase(c.line, c.col);
  } else {
    const auto source = args.at("source").get<std::string>(),
               target = args.at("target").get<std::string>();
    const auto output = args.at("output").get<std::string>(),
               input = args.at("input").get<std::string>();
    bool found = false;
    for (const auto &c : ast.cables) {
      bool default_output = false;
      for (const auto &n : graph()->nodes)
        if (n.name == source && !n.info->outputs.empty())
          default_output = output == n.info->outputs.front().name;
      if (!c.src.is_literal &&
          (c.src.module == source + "." + output ||
           (default_output && c.src.module == source)) &&
          (c.dst.module == target + "." + input ||
           (target == "out" && c.dst.module == "out.in"))) {
        erase(c.line, c.col);
        found = true;
        break;
      }
    }
    if (!found)
      throw std::runtime_error(
          "This connection belongs to an expanded subpatch; edit its source");
  }
  std::sort(removals.rbegin(), removals.rend());
  std::string text = source_;
  for (const auto &[at, length] : removals)
    text.erase(at, length);
  return apply(text, revision_);
}
Json Session::interfaces() {
  Json result = Json::array();
  for (const auto *m : dsp::all_modules()) {
    Json inputs = Json::array(), outputs = Json::array(),
         configs = Json::array();
    for (const auto &i : m->inputs)
      inputs.push_back({{"name", i.name}, {"base", i.def}, {"glide", i.glide}});
    for (const auto &o : m->outputs)
      outputs.push_back(o.name);
    for (const auto &c : m->configs) {
      using K = dsp::ConfigKind;
      Json config = {{"name", c.name},
                     {"kind", c.kind == K::Enums     ? "enum_array"
                              : c.enum_vals          ? "enum"
                              : c.kind == K::Numbers ? "number_array"
                              : c.kind == K::Text    ? "text"
                                                     : "number"}};
      if (c.enum_vals) {
        config["values"] = Json::array();
        for (auto e = c.enum_vals; *e; ++e)
          config["values"].push_back(*e);
      }
      configs.push_back(config);
    }
    result.push_back({{"type", m->type},
                      {"inputs", inputs},
                      {"outputs", outputs},
                      {"configs", configs}});
  }
  return result;
}
Json Session::state() const {
  Json modules = Json::array(), cables = Json::array();
  size_t at = 0;
  const bool fresh = observation_.generation == controller_.generation();
  if (graph()) {
    for (const auto &n : graph()->nodes) {
      Json bases = Json::object(), outputs = Json::object();
      for (size_t i = 0; i < n.input_base.size(); ++i)
        bases[n.info->inputs[i].name] = n.input_base[i];
      for (const auto &o : n.info->outputs) {
        if (fresh && at < observation_.size)
          outputs[o.name] = observation_.values[at];
        else
          outputs[o.name] = nullptr;
        ++at;
      }
      modules.push_back({{"name", n.name},
                         {"type", n.info->type},
                         {"inputs", bases},
                         {"outputs", outputs}});
    }
    for (const auto &incoming : graph()->in_cables)
      for (const auto &c : incoming)
        cables.push_back(
            {{"source", graph()->nodes[static_cast<size_t>(c.src_mod)].name},
             {"output", graph()
                            ->nodes[static_cast<size_t>(c.src_mod)]
                            .info->outputs[static_cast<size_t>(c.src_out)]
                            .name},
             {"target", graph()->nodes[static_cast<size_t>(c.dst_mod)].name},
             {"input", graph()
                           ->nodes[static_cast<size_t>(c.dst_mod)]
                           .info->inputs[static_cast<size_t>(c.dst_in)]
                           .name},
             {"depth", c.depth}});
  }
  return {{"revision", revision_},
          {"audio", audio_status_},
          {"path", path_},
          {"source", source_},
          {"dirty", dirty()},
          {"can_undo", !undo_.empty()},
          {"can_redo", !redo_.empty()},
          {"message", message_},
          {"modules", modules},
          {"cables", cables},
          {"peak_l", runner_.peak_l()},
          {"peak_r", runner_.peak_r()},
          {"observed_frame", fresh ? observation_.frame : 0},
          {"telemetry_truncated", at > observation_.values.size()}};
}
Json Session::call(const std::string &method, const Json &args) {
  if (method == "undo" || method == "redo") {
    require_revision(args.at("revision").get<uint64_t>());
    auto &from = method == "undo" ? undo_ : redo_;
    auto &to = method == "undo" ? redo_ : undo_;
    if (from.empty())
      throw std::runtime_error("No edit to " + method);
    const std::string previous = source_, next = from.back();
    auto result = apply(next, revision_, false);
    from.pop_back();
    to.push_back(previous);
    return result;
  }
  if (method == "reference")
    return {{"text", read(RATSNEST_REFERENCE_PATH)}};
  if (method == "state")
    return state();
  if (method == "describe")
    return {{"modules", interfaces()}};
  if (method == "replace_patch")
    return apply(args.at("source").get<std::string>(),
                 args.at("revision").get<uint64_t>());
  if (method == "set_parameter")
    return set_param(args);
  if (method == "remove_node")
    return remove(args, true);
  if (method == "disconnect")
    return remove(args, false);
  if (method == "save")
    return save(args.at("revision").get<uint64_t>());
  throw std::runtime_error("Unknown operation: " + method);
}
} // namespace ms::studio
