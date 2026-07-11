#include <filesystem>
#include <fstream>
#include <iterator>
#include <string>
#include <doctest/doctest.h>
#include "dsp/modules.h"

using namespace ms::dsp;

TEST_CASE("module reference covers every registered module") {
  const auto root = std::filesystem::path(__FILE__).parent_path().parent_path();
  std::ifstream file(root / "docs/modules.md");
  REQUIRE(file.good());
  const std::string text((std::istreambuf_iterator<char>(file)),
                         std::istreambuf_iterator<char>());
  CHECK(text.find("## Signal conventions") != std::string::npos);
  CHECK(text.find("## DSL essentials") != std::string::npos);
  CHECK(text.find("## Recipes") != std::string::npos);
  for (const ModuleInfo* info : all_modules()) {
    CAPTURE(info->type);
    CHECK(text.find("## `" + std::string(info->type) + "`") !=
          std::string::npos);
  }
}
