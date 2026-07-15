#include <filesystem>
#include <fstream>
#include <iterator>
#include <string>
#include <doctest/doctest.h>
#include "dsp/modules.h"

using namespace ms::dsp;

namespace {
const auto kRoot = std::filesystem::path(__FILE__).parent_path().parent_path();

std::string read_repo_file(const std::filesystem::path& relative) {
  std::ifstream file(kRoot / relative);
  INFO("reading " << (kRoot / relative));
  REQUIRE(file.good());
  return std::string(std::istreambuf_iterator<char>(file),
                     std::istreambuf_iterator<char>());
}

void check_contains(const std::string& text, const std::string& needle) {
  CAPTURE(needle);
  CHECK(text.find(needle) != std::string::npos);
}
}  // namespace

TEST_CASE("module reference covers every registered module") {
  const std::string text = read_repo_file("docs/modules.md");
  CHECK(text.find("## Signal conventions") != std::string::npos);
  CHECK(text.find("## DSL essentials") != std::string::npos);
  CHECK(text.find("## Recipes") != std::string::npos);
  for (const ModuleInfo* info : all_modules()) {
    CAPTURE(info->type);
    CHECK(text.find("## `" + std::string(info->type) + "`") !=
          std::string::npos);
  }
}

TEST_CASE("license and attribution records cover implementation provenance") {
  const std::string license = read_repo_file("LICENSE");
  check_contains(license, "Copyright (c) 2026 Ben Sandbrook");
  check_contains(license, "Permission is hereby granted");

  const std::string notices = read_repo_file("THIRD_PARTY_NOTICES.md");
  for (const char* required : {
           "Copyright 2015 Emilie Gillet",
           "Copyright 2012 Emilie Gillet",
           "BSD-2-Clause",
           "Copyright (c) 2013-2021, Cameron Desrochers",
           "Copyright © 2018 Wim Taymans",
           "Copyright (c) 2016-2023 Viktor Kirilov",
       }) {
    check_contains(notices, required);
  }

  const std::string attribution = read_repo_file("docs/attribution.md");
  for (const char* required : {
           "Mutable Instruments Rings", "Karplus", "Dattorro",
           "Giannoulis", "Bristow-Johnson", "Parker", "Vicanek",
           "polyBLEP", "Zavalishin", "Simper", "Toussaint",
           "Marsaglia", "readerwriterqueue", "PipeWire", "doctest",
       }) {
    check_contains(attribution, required);
  }

  for (const char* source : {
           "src/dsp/resonate.h",
           "src/dsp/modules/modal.cpp",
           "src/dsp/modules/string.cpp",
           "src/rt/spsc.h",
       }) {
    const std::string text = read_repo_file(source);
    check_contains(text, "docs/attribution.md");
    check_contains(text, "THIRD_PARTY_NOTICES.md");
  }
}
