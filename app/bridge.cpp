#include "bridge.h"
#include <cerrno>
#include <chrono>
#include <cstring>
#include <filesystem>
#include <iostream>
#include <stdexcept>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/un.h>
#include <unistd.h>

namespace ms::studio {
namespace {
uint64_t now() {
  return static_cast<uint64_t>(
      std::chrono::duration_cast<std::chrono::milliseconds>(
          std::chrono::steady_clock::now().time_since_epoch())
          .count());
}
sockaddr_un address(const std::string &path) {
  sockaddr_un a{};
  a.sun_family = AF_UNIX;
  if (path.size() >= sizeof(a.sun_path))
    throw std::runtime_error("Socket path too long");
  std::memcpy(a.sun_path, path.c_str(), path.size() + 1);
  return a;
}
Json remote(const std::string &path, const std::string &method,
            const Json &args) {
  const auto a = address(path);
  int fd = socket(AF_UNIX, SOCK_STREAM | SOCK_CLOEXEC, 0);
  if (fd < 0)
    throw std::runtime_error("Cannot create local connection");
  struct Close {
    int fd;
    ~Close() { close(fd); }
  } guard{fd};
  timeval timeout{3, 0};
  setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof(timeout));
  setsockopt(fd, SOL_SOCKET, SO_SNDTIMEO, &timeout, sizeof(timeout));
  if (connect(fd, reinterpret_cast<const sockaddr *>(&a), sizeof(a)) < 0)
    throw std::runtime_error("Cannot connect to studio at " + path + ": " +
                             strerror(errno));
  const std::string request =
      Json{{"method", method}, {"arguments", args}}.dump() + "\n";
  size_t sent = 0;
  while (sent < request.size()) {
    const auto n =
        send(fd, request.data() + sent, request.size() - sent, MSG_NOSIGNAL);
    if (n <= 0)
      throw std::runtime_error("Studio send failed");
    sent += static_cast<size_t>(n);
  }
  std::string result;
  char buffer[8192];
  while (result.find('\n') == std::string::npos) {
    const auto n = recv(fd, buffer, sizeof(buffer), 0);
    if (n <= 0)
      throw std::runtime_error("Studio response timed out or closed");
    result.append(buffer, static_cast<size_t>(n));
    if (result.size() > 16 * 1024 * 1024)
      throw std::runtime_error("Studio response exceeds 16 MiB");
  }
  const auto response = Json::parse(result);
  if (!response.at("ok").get<bool>())
    throw std::runtime_error(response.at("error").get<std::string>());
  return response.at("result");
}
Json tools() {
  const Json rev = {
      {"type", "integer"},
      {"minimum", 0},
      {"description",
       "Revision from the most recent state; stale edits are rejected"}};
  Json list = Json::array();
  auto add = [&](const char *name, const char *description, Json properties,
                 Json required, bool readOnly) {
    list.push_back({{"name", name},
                    {"description", description},
                    {"inputSchema",
                     {{"type", "object"},
                      {"properties", properties},
                      {"required", required},
                      {"additionalProperties", false}}},
                    {"annotations",
                     {{"readOnlyHint", readOnly},
                      {"destructiveHint", !readOnly},
                      {"openWorldHint", false}}}});
  };
  add("state",
      "Inspect the live graph, patch source, revision, channel meters and "
      "scene/transport outputs. Null outputs have not been observed yet.",
      Json::object(), Json::array(), true);
  add("describe",
      "Discover module input bases, glide behavior, config types and enum "
      "choices.",
      Json::object(), Json::array(), true);
  add("set_parameter",
      "Edit one authored module parameter using a DSL value (examples: 0.5, "
      "sine, [0,3,7]). Validated changes become audible; use save separately "
      "to persist.",
      {{"revision", rev},
       {"module", {{"type", "string"}}},
       {"parameter", {{"type", "string"}}},
       {"value", {{"type", "string"}}}},
      {"revision", "module", "parameter", "value"}, false);
  add("replace_patch",
      "Validate complete patch source before queuing the edit. Invalid patches "
      "keep the last good graph. Changes are live and unsaved until save.",
      {{"revision", rev}, {"source", {{"type", "string"}}}},
      {"revision", "source"}, false);
  add("save",
      "Save the current live source to its existing patch file. Rejects "
      "external file changes to prevent overwriting concurrent work.",
      {{"revision", rev}}, {"revision"}, false);
  return list;
}
} // namespace
std::string default_socket(const std::string &patch) {
  const char *runtime = getenv("XDG_RUNTIME_DIR");
  const auto key =
      std::hash<std::string>{}(std::filesystem::canonical(patch).string());
  return std::string(runtime ? runtime : "/tmp") + "/ratsnest-" +
         std::to_string(getuid()) + "-" + std::to_string(key) + ".sock";
}
Bridge::Bridge(Session &session, std::string path)
    : session_(session), path_(std::move(path)) {
  const auto a = address(path_);
  fd_ = socket(AF_UNIX, SOCK_STREAM | SOCK_NONBLOCK | SOCK_CLOEXEC, 0);
  if (fd_ < 0)
    throw std::runtime_error("Cannot create studio socket");
  if (bind(fd_, reinterpret_cast<const sockaddr *>(&a), sizeof(a)) < 0) {
    const std::string reason = strerror(errno);
    close(fd_);
    fd_ = -1;
    throw std::runtime_error("Cannot bind " + path_ + ": " + reason);
  }
  chmod(path_.c_str(), 0600);
  if (listen(fd_, 8) < 0) {
    close(fd_);
    unlink(path_.c_str());
    fd_ = -1;
    throw std::runtime_error("Cannot listen on studio socket");
  }
}
Bridge::~Bridge() {
  for (auto &c : clients_)
    close(c.fd);
  if (fd_ >= 0) {
    close(fd_);
    unlink(path_.c_str());
  }
}
void Bridge::poll() {
  while (clients_.size() < 16) {
    int client = accept4(fd_, nullptr, nullptr, SOCK_NONBLOCK | SOCK_CLOEXEC);
    if (client < 0)
      break;
    clients_.push_back({client, {}, {}, 0, now()});
  }
  for (auto it = clients_.begin(); it != clients_.end();) {
    bool done = now() - it->started > 5000;
    if (it->output.empty() && !done) {
      char buffer[8192];
      size_t budget = 65536;
      while (budget) {
        const auto n =
            recv(it->fd, buffer, std::min(sizeof(buffer), budget), 0);
        if (n <= 0) {
          if (n == 0 || (errno != EAGAIN && errno != EWOULDBLOCK))
            done = true;
          break;
        }
        it->input.append(buffer, static_cast<size_t>(n));
        budget -= static_cast<size_t>(n);
        if (it->input.find('\n') != std::string::npos)
          break;
      }
      if (it->input.size() > 8 * 1024 * 1024)
        done = true;
      const auto newline = it->input.find('\n');
      if (newline != std::string::npos && !done) {
        Json response;
        try {
          const auto request = Json::parse(it->input.substr(0, newline));
          response = {
              {"ok", true},
              {"result",
               session_.call(request.at("method").get<std::string>(),
                             request.value("arguments", Json::object()))}};
        } catch (const std::exception &e) {
          response = {{"ok", false}, {"error", e.what()}};
        }
        it->output = response.dump() + "\n";
      }
    }
    if (!it->output.empty() && !done) {
      const auto n = send(it->fd, it->output.data() + it->sent,
                          std::min(size_t{65536}, it->output.size() - it->sent),
                          MSG_NOSIGNAL);
      if (n > 0)
        it->sent += static_cast<size_t>(n);
      else if (n < 0 && errno != EAGAIN && errno != EWOULDBLOCK)
        done = true;
      if (it->sent == it->output.size())
        done = true;
    }
    if (done) {
      close(it->fd);
      it = clients_.erase(it);
    } else
      ++it;
  }
}
int mcp(const std::string &path) {
  std::string line;
  bool initialized = false;
  while (std::getline(std::cin, line)) {
    Json id = nullptr, response;
    try {
      if (line.size() > 8 * 1024 * 1024)
        throw std::runtime_error("Request exceeds 8 MiB");
      const auto req = Json::parse(line);
      if (!req.is_object() || req.value("jsonrpc", "") != "2.0")
        throw std::runtime_error("Invalid JSON-RPC request");
      if (!req.contains("id"))
        continue;
      id = req.at("id");
      const auto method = req.at("method").get<std::string>();
      Json result;
      if (method == "initialize") {
        initialized = true;
        result = {{"protocolVersion", "2025-11-25"},
                  {"capabilities",
                   {{"tools", Json::object()}, {"resources", Json::object()}}},
                  {"serverInfo", {{"name", "ratsnest"}, {"version", "0.1.0"}}},
                  {"instructions",
                   "Read state before editing. Every mutation requires its "
                   "revision. UI and agent share one live patch. Save only "
                   "when persistence is intended."}};
      } else if (method == "ping")
        result = Json::object();
      else if (!initialized)
        throw std::runtime_error("Initialize first");
      else if (method == "resources/list")
        result = {
            {"resources",
             Json::array(
                 {{{"uri", "ratsnest://modules"},
                   {"name", "Module reference"},
                   {"mimeType", "text/markdown"},
                   {"description",
                    "Units, timing semantics, ranges and patch recipes"}}})}};
      else if (method == "resources/read") {
        if (req.at("params").at("uri") != "ratsnest://modules")
          throw std::runtime_error("Unknown resource");
        result = {
            {"contents",
             Json::array(
                 {{{"uri", "ratsnest://modules"},
                   {"mimeType", "text/markdown"},
                   {"text",
                    remote(path, "reference", Json::object()).at("text")}}})}};
      } else if (method == "tools/list")
        result = {{"tools", tools()}};
      else if (method == "tools/call") {
        const auto &p = req.at("params");
        const auto name = p.at("name").get<std::string>();
        bool known = false;
        for (const auto &tool : tools())
          if (tool["name"] == name)
            known = true;
        if (!known) {
          std::cout << Json{{"jsonrpc", "2.0"},
                            {"id", id},
                            {"error",
                             {{"code", -32602},
                              {"message", "Unknown tool: " + name}}}}
                           .dump()
                    << '\n'
                    << std::flush;
          continue;
        }
        try {
          auto data = remote(path, name, p.value("arguments", Json::object()));
          result = {{"content",
                     Json::array({{{"type", "text"}, {"text", data.dump()}}})},
                    {"structuredContent", data},
                    {"isError", false}};
        } catch (const std::exception &e) {
          result = {{"content",
                     Json::array({{{"type", "text"}, {"text", e.what()}}})},
                    {"isError", true}};
        }
      } else {
        std::cout << Json{{"jsonrpc", "2.0"},
                          {"id", id},
                          {"error",
                           {{"code", -32601}, {"message", "Method not found"}}}}
                         .dump()
                  << '\n'
                  << std::flush;
        continue;
      }
      response = {{"jsonrpc", "2.0"}, {"id", id}, {"result", result}};
    } catch (const Json::parse_error &e) {
      response = {{"jsonrpc", "2.0"},
                  {"id", nullptr},
                  {"error", {{"code", -32700}, {"message", e.what()}}}};
    } catch (const std::exception &e) {
      response = {{"jsonrpc", "2.0"},
                  {"id", id},
                  {"error", {{"code", -32600}, {"message", e.what()}}}};
    }
    std::cout << response.dump() << '\n' << std::flush;
  }
  return 0;
}
} // namespace ms::studio
