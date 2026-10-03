#pragma once
#include "session.h"
#include <vector>
namespace ms::studio {
std::string default_socket(const std::string &patch);
class Bridge {
public:
  Bridge(Session &session, std::string path);
  ~Bridge();
  void poll();
  const std::string &path() const { return path_; }

private:
  struct Client {
    int fd;
    std::string input, output;
    size_t sent = 0;
    uint64_t started = 0;
  };
  Session &session_;
  std::string path_;
  int fd_ = -1;
  std::vector<Client> clients_;
};
int mcp(const std::string &socket);
} // namespace ms::studio
