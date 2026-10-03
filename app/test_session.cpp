#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include "session.h"
#include <doctest/doctest.h>
#include <filesystem>
#include <fstream>
#include <unistd.h>

using namespace ms::studio;
namespace {
struct Patch {
  std::string path =
      "/tmp/ratsnest-session-test-" + std::to_string(getpid()) + ".patch";
  explicit Patch(const std::string &text) { std::ofstream(path) << text; }
  ~Patch() { std::filesystem::remove(path); }
};
void pump(Session &s) {
  float l[4096], r[4096];
  s.runner().render(l, r, 4096);
  s.poll();
}
} // namespace
TEST_CASE("shared session edits preserve comments reject stale revisions and "
          "retain last good graph") {
  Patch file("# header\nv = vco { freq: 110hz # keep this\n, wave: sine }\nv "
             "-> out.in : 0.2\n");
  Session s(file.path);
  pump(s);
  s.call("set_parameter", {{"revision", 1},
                           {"module", "v"},
                           {"parameter", "freq"},
                           {"value", "220hz"}});
  CHECK(s.source().find("220hz # keep this") != std::string::npos);
  CHECK(s.revision() == 2);
  CHECK(s.dirty());
  CHECK_THROWS(s.call("set_parameter", {{"revision", 1},
                                        {"module", "v"},
                                        {"parameter", "freq"},
                                        {"value", "330"}}));
  CHECK_THROWS(s.call("replace_patch",
                      {{"revision", 2}, {"source", "v = nonexistent {}"}}));
  CHECK(s.revision() == 2);
  CHECK_THROWS(
      s.call("set_parameter", {{"revision", 2},
                               {"module", "v"},
                               {"parameter", "freq"},
                               {"value", "3 }\nhacked = vco {freq: 1"}}));
  CHECK(s.source().find("hacked") == std::string::npos);
  pump(s);
  s.call("save", {{"revision", 2}});
  CHECK_FALSE(s.dirty());
}
TEST_CASE("parameter insertion handles bare declarations comments arrays and "
          "trailing commas") {
  Patch file("v = vco # bare\np = phrase { values: [1, 2, 3], freeze: 1, }\nv "
             "-> out.in\n");
  Session s(file.path);
  pump(s);
  s.call("set_parameter", {{"revision", 1},
                           {"module", "v"},
                           {"parameter", "freq"},
                           {"value", "123"}});
  CHECK(s.source().find("vco { freq: 123 } # bare") != std::string::npos);
  s.call("set_parameter", {{"revision", 2},
                           {"module", "p"},
                           {"parameter", "length"},
                           {"value", "3"}});
  pump(s);
  s.call("set_parameter", {{"revision", 3},
                           {"module", "p"},
                           {"parameter", "values"},
                           {"value", "[4,5]"}});
  CHECK(s.source().find("values: [4,5], freeze") != std::string::npos);
}
TEST_CASE("save refuses external edits and state includes observed outputs") {
  Patch file("v = vco {freq: 0, phase: 0.25}\nv -> out.in\n");
  Session s(file.path);
  pump(s);
  auto state = s.state();
  CHECK(state["observed_frame"].get<int>() > 0);
  CHECK(state["modules"].size() == 2);
  std::ofstream(file.path) << "# external edit\n";
  CHECK_THROWS(s.call("save", {{"revision", 1}}));
}

TEST_CASE("graph deletion preserves unrelated comments and removes only owned "
          "cables") {
  Patch file("a = vco {}\n# about b\nb = vco {}\na -> out.in\nb -> out.in\n");
  Session s(file.path);
  pump(s);
  s.call("disconnect", {{"revision", 1},
                        {"source", "a"},
                        {"output", "out"},
                        {"target", "out"},
                        {"input", "left"}});
  CHECK(s.source().find("a -> out.in") == std::string::npos);
  pump(s);
  s.call("remove_node", {{"revision", 2}, {"module", "a"}});
  CHECK(s.source().find("# about b") != std::string::npos);
  CHECK(s.source().find("b -> out.in") != std::string::npos);
}

TEST_CASE("undo and redo retain monotonic revisions across collaborators") {
  Patch file("v = vco {freq: 110}\nv -> out.in\n");
  Session s(file.path);
  pump(s);
  s.call("set_parameter", {{"revision", 1},
                           {"module", "v"},
                           {"parameter", "freq"},
                           {"value", "220"}});
  s.call("undo", {{"revision", 2}});
  CHECK(s.source().find("freq: 110") != std::string::npos);
  CHECK(s.revision() == 3);
  s.call("redo", {{"revision", 3}});
  CHECK(s.source().find("freq: 220") != std::string::npos);
  CHECK(s.revision() == 4);
  CHECK_THROWS(s.call("undo", {{"revision", 3}}));
}
