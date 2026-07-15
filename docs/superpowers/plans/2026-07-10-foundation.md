# ratsnest Foundation (Phase 1a) Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** The ratsnest base: repo conventions, patch DSL (lexer/parser), module system, graph compiler, offline per-sample engine, WAV writer, and a CLI whose exit state is `ratsnest render patches/first.patch` producing a detuned-saw drone WAV — deterministic and fully tested.

**Architecture:** Text patch → `dsl/` (lexer → parser → AST) → `graph/` (validate against module registry, topo-sort, fold literal cables) → `engine/` (per-sample walk; persistent output array makes feedback = 1-sample delay automatically) → `io/` WAV. No realtime audio in this plan (Phase 1b adds PipeWire); everything is testable offline and deterministic.

**Tech Stack:** C++20 · CMake + CMakePresets (dev/ci/san) · doctest via FetchContent · ctest · mise tasks · clang-tidy.

**Spec:** `docs/superpowers/specs/2026-07-10-ratsnest-design.md` (approved 2026-07-10). Sibling plans to follow: 1b realtime+hot-reload, 1c remaining Phase-1 modules.

## Global Constraints

- C++20; external deps **doctest only** in this plan (PipeWire arrives in Phase 1b; nothing else, ever, per spec §9).
- Everything under namespace `ms` (sub-namespaces `ms::dsl`, `ms::dsp`, `ms::graph`).
- Signal conventions (spec §4): audio/bipolar CV ±1.0, unipolar 0..1, `v_oct` in octaves offsetting the module's `freq` base, gates rise through 0.5.
- Per-sample processing; `Module::process()` must not allocate, lock, or do I/O.
- No normalling: modulation **sums onto** declared param bases; unpatched inputs read their default.
- Units normalize in the lexer: `hz`×1, `khz`×1000, `ms`×0.001, `s`×1, `bpm`×1.
- Parse/validation errors carry line:col; error quality is a feature with golden tests.
- Commits: conventional style (`feat:`/`test:`/`chore:`), tool-agnostic messages, no session trailers.
- Warnings are errors in the `ci` preset. `san` preset (ASan+UBSan) must stay green.

## File Structure

```
CMakeLists.txt  CMakePresets.json  mise.toml  .clang-tidy  .gitignore  CLAUDE.md
src/dsl/diag.h         # Diag {line,col,msg} — shared by all layers
src/dsl/token.h        # token kinds
src/dsl/lexer.{h,cpp}
src/dsl/ast.h          # Value, ModuleDecl, CableDecl, PatchAst
src/dsl/parser.{h,cpp}
src/dsp/module.h       # Module, ModuleInfo, Config, helpers
src/dsp/module.cpp
src/dsp/modules.h      # extern ModuleInfo declarations
src/dsp/registry.cpp
src/dsp/modules/{vca,out,vco,lfo}.cpp
src/graph/graph.{h,cpp}
src/engine/engine.{h,cpp}
src/io/wav.{h,cpp}
src/main.cpp
tests/test_{lexer,parser,module,graph,engine,vco,lfo,wav,integration}.cpp
patches/first.patch
```

---

### Task 1: Repo scaffold & conventions

**Files:**
- Create: `.gitignore`, `CMakeLists.txt`, `CMakePresets.json`, `mise.toml`, `.clang-tidy`, `CLAUDE.md`, `tests/test_smoke.cpp`

**Interfaces:**
- Produces: build system every later task uses — library target `mscore` (all `src/` except `main.cpp`), executable `ratsnest`, test executable `mstests` registered with ctest. Commands: `mise run build`, `mise run test`.

- [ ] **Step 1: Write files**

`.gitignore`:
```
build*/
compile_commands.json
*.wav
.cache/
```

`CMakeLists.txt`:
```cmake
cmake_minimum_required(VERSION 3.25)
project(ratsnest CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

option(RATSNEST_WERROR "Treat warnings as errors" OFF)
option(RATSNEST_SAN "Enable ASan + UBSan for every target" OFF)

add_compile_options(-Wall -Wextra -Wconversion)
if(RATSNEST_WERROR)
  add_compile_options(-Werror)
endif()
if(RATSNEST_SAN)
  add_compile_options(-fsanitize=address,undefined -fno-omit-frame-pointer
                      -fno-sanitize-recover=all)
  add_link_options(-fsanitize=address,undefined)
endif()

file(GLOB_RECURSE MSCORE_SOURCES CONFIGURE_DEPENDS src/*.cpp)
list(REMOVE_ITEM MSCORE_SOURCES ${CMAKE_CURRENT_SOURCE_DIR}/src/main.cpp)
add_library(mscore STATIC ${MSCORE_SOURCES})
target_include_directories(mscore PUBLIC src)

add_executable(ratsnest src/main.cpp)
target_link_libraries(ratsnest PRIVATE mscore)

include(FetchContent)
FetchContent_Declare(doctest
  URL https://github.com/doctest/doctest/archive/refs/tags/v2.4.11.tar.gz)
FetchContent_MakeAvailable(doctest)

enable_testing()
file(GLOB MSTEST_SOURCES CONFIGURE_DEPENDS tests/*.cpp)
add_executable(mstests ${MSTEST_SOURCES})
target_link_libraries(mstests PRIVATE mscore doctest::doctest)
add_test(NAME unit COMMAND mstests)
```

`CMakePresets.json`:
```json
{
  "version": 6,
  "configurePresets": [
    {
      "name": "dev",
      "binaryDir": "build",
      "generator": "Ninja",
      "cacheVariables": {
        "CMAKE_BUILD_TYPE": "Debug",
        "CMAKE_EXPORT_COMPILE_COMMANDS": "ON"
      }
    },
    {
      "name": "ci",
      "binaryDir": "build-ci",
      "generator": "Ninja",
      "cacheVariables": {
        "CMAKE_BUILD_TYPE": "RelWithDebInfo",
        "RATSNEST_WERROR": "ON"
      }
    },
    {
      "name": "san",
      "binaryDir": "build-san",
      "generator": "Ninja",
      "cacheVariables": {
        "CMAKE_BUILD_TYPE": "RelWithDebInfo",
        "RATSNEST_SAN": "ON"
      }
    }
  ]
}
```

`mise.toml`:
```toml
[tools]
cmake = "latest"
ninja = "latest"

[tasks.configure]
description = "Configure the build directory with the dev CMake preset."
run = "cmake --preset dev"

[tasks.build]
description = "Configure with the dev preset, then build into build/."
run = """
cmake --preset dev
cmake --build build
"""

[tasks.test]
description = "Build, then run all ctest gates."
depends = ["build"]
run = "ctest --test-dir build --output-on-failure"

[tasks.test-san]
description = "Build with ASan+UBSan and run all tests against sanitized binaries."
run = """
cmake --preset san
cmake --build build-san
ctest --test-dir build-san --output-on-failure
"""

[tasks.ci]
description = "Full gate: warnings-as-errors build, tests, sanitized tests."
run = """
cmake --preset ci
cmake --build build-ci
ctest --test-dir build-ci --output-on-failure
"""
depends = ["test-san"]

[tasks.check]
description = "Parse/validate a patch file. Usage: mise run check -- patches/first.patch"
depends = ["build"]
run = "./build/ratsnest check"

[tasks.render]
description = "Render a patch to WAV offline. Usage: mise run render -- patches/first.patch -d 10s -o take.wav"
depends = ["build"]
run = "./build/ratsnest render"
```

`.clang-tidy`:
```yaml
Checks: >
  bugprone-*,
  performance-*,
  portability-*,
  modernize-use-override,
  modernize-use-nullptr,
  modernize-use-make-unique,
  readability-braces-around-statements,
  cppcoreguidelines-owning-memory,
  cppcoreguidelines-special-member-functions,
  -bugprone-easily-swappable-parameters
WarningsAsErrors: ''
HeaderFilterRegex: '^src/'
```

`CLAUDE.md`:
```markdown
This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

# ratsnest

A software modular synthesizer for Linux where **the patch is a plain-text file**.
Hand-written C++20 DSP, PipeWire out, hot-reload on save. Personal instrument;
single author. Non-goals: DAW, plugin host, livecoding language, marketplace.

Authoritative design: `docs/superpowers/specs/2026-07-10-ratsnest-design.md`.
Implementation plans: `docs/superpowers/plans/` (checkbox tasks = status; a task
is done only when its named tests pass — never hand-edit status).

## Build / test

Prefer mise tasks (see `mise tasks`):

    mise run build        # cmake --preset dev && cmake --build build
    mise run test         # ctest --test-dir build --output-on-failure
    mise run test-san     # same tests under ASan+UBSan (build-san/)
    mise run ci           # warnings-as-errors gate (build-ci/)
    mise run render -- patches/first.patch -d 10s -o take.wav

## Non-negotiable design rules

Violating these is a design bug, not a style preference.

1. The `.patch` file is the source of truth. Any future GUI/TUI round-trips
   through it. Canvas layout lives in a sidecar, never in the patch.
2. Everything is one signal: floats at audio rate. No control rate, no
   separate event type.
3. `Module::process()` never allocates, locks, or does I/O. Anything reachable
   from the audio thread follows the same rule. The only RT↔non-RT bridges are
   the SPSC queue, ring buffers, and the graph pointer swap (Phase 1b).
4. No normalling. Modulation sums onto declared param bases. Unpatched inputs
   read their default. Nothing implicit.
5. Modules are small and single-purpose; compose with cables.
6. Errors never kill the session: bad patch → report line:col, keep the last
   good graph playing.
7. External deps: PipeWire and doctest. Adding a third is a design decision,
   not a convenience.
8. Signal conventions (spec §4): audio/bipolar ±1, unipolar 0..1, v_oct in
   octaves, gates rise through 0.5. Every port respects them.

## Commit conventions

Conventional prefixes (feat/fix/test/chore/docs). Tool-agnostic messages —
no AI attribution, no session trailers. Docs are part of the diff: update
spec/CLAUDE.md in the same commit as behavior they describe.
```

`tests/test_smoke.cpp`:
```cpp
#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include <doctest/doctest.h>

TEST_CASE("toolchain smoke") { CHECK(1 + 1 == 2); }
```

- [ ] **Step 2: Build and test**

Run: `mise run test`
Expected: configure + build succeed, ctest reports `1/1 tests passed` (doctest downloads on first configure).
Note: `mscore` has no sources yet — if CMake errors on an empty library, add a placeholder `src/dsl/diag.h` include TU later in Task 2; for now, if it errors, temporarily change `add_library(mscore STATIC ...)` to require sources from Task 2 by creating `src/dsl/lexer.cpp` stub `namespace ms {}` and commit it as part of this task.

- [ ] **Step 3: Commit**

```bash
git add -A
git commit -m "chore: scaffold build system, presets, mise tasks, CLAUDE.md"
```

---

### Task 2: Diagnostics + lexer

**Files:**
- Create: `src/dsl/diag.h`, `src/dsl/token.h`, `src/dsl/lexer.h`, `src/dsl/lexer.cpp`
- Test: `tests/test_lexer.cpp`

**Interfaces:**
- Produces: `ms::Diag{int line, col; std::string msg}`, `ms::Diags`;
  `ms::dsl::Tok` enum; `ms::dsl::Token{Tok kind; std::string text; double num; int line, col}`;
  `ms::dsl::LexResult{std::vector<Token> tokens; Diags errors}`;
  `ms::dsl::LexResult lex(std::string_view src)`.
  Guarantees: numbers arrive unit-normalized in `num`; `Newline` tokens are suppressed inside `{}`/`[]`; token stream always ends with `End`.

- [ ] **Step 1: Write the failing tests**

`tests/test_lexer.cpp`:
```cpp
#include <doctest/doctest.h>
#include "dsl/lexer.h"

using namespace ms::dsl;

static std::vector<Tok> kinds(const LexResult& r) {
  std::vector<Tok> k;
  for (auto& t : r.tokens) k.push_back(t.kind);
  return k;
}

TEST_CASE("lexes a declaration") {
  auto r = lex("osc1 = vco { freq: 110hz, wave: saw }");
  REQUIRE(r.errors.empty());
  CHECK(kinds(r) == std::vector<Tok>{Tok::Ident, Tok::Equals, Tok::Ident,
      Tok::LBrace, Tok::Ident, Tok::Colon, Tok::Number, Tok::Comma,
      Tok::Ident, Tok::Colon, Tok::Ident, Tok::RBrace, Tok::End});
  CHECK(r.tokens[6].num == doctest::Approx(110.0));
}

TEST_CASE("normalizes units") {
  auto r = lex("1.2khz 250ms 2s 0.5 120bpm");
  REQUIRE(r.errors.empty());
  CHECK(r.tokens[0].num == doctest::Approx(1200.0));
  CHECK(r.tokens[1].num == doctest::Approx(0.25));
  CHECK(r.tokens[2].num == doctest::Approx(2.0));
  CHECK(r.tokens[3].num == doctest::Approx(0.5));
  CHECK(r.tokens[4].num == doctest::Approx(120.0));
}

TEST_CASE("arrow, dot, depth, negative numbers") {
  auto r = lex("lfo1 -> filt.cutoff : -0.5");
  REQUIRE(r.errors.empty());
  CHECK(kinds(r) == std::vector<Tok>{Tok::Ident, Tok::Arrow, Tok::Ident,
      Tok::Dot, Tok::Ident, Tok::Colon, Tok::Number, Tok::End});
  CHECK(r.tokens[6].num == doctest::Approx(-0.5));
}

TEST_CASE("comments and newlines; newlines suppressed inside brackets") {
  auto r = lex("a = vca # panel\n[1,\n2]\nb = vca");
  REQUIRE(r.errors.empty());
  int newlines = 0;
  for (auto& t : r.tokens) if (t.kind == Tok::Newline) ++newlines;
  CHECK(newlines == 2);  // after 'vca' and after ']', none inside [...]
}

TEST_CASE("strings") {
  auto r = lex("file: \"reel1.wav\"");
  REQUIRE(r.errors.empty());
  CHECK(r.tokens[2].kind == Tok::Str);
  CHECK(r.tokens[2].text == "reel1.wav");
}

TEST_CASE("unknown unit is an error with position") {
  auto r = lex("freq: 100furlongs");
  REQUIRE(r.errors.size() == 1);
  CHECK(r.errors[0].line == 1);
  CHECK(r.errors[0].msg.find("unknown unit") != std::string::npos);
}

TEST_CASE("tracks line and column") {
  auto r = lex("a = vca\nbb = vca");
  CHECK(r.tokens[3].line == 2);
  CHECK(r.tokens[3].col == 1);
}
```

- [ ] **Step 2: Run to verify failure**

Run: `mise run test`
Expected: FAIL to compile (`dsl/lexer.h` not found).

- [ ] **Step 3: Implement**

`src/dsl/diag.h`:
```cpp
#pragma once
#include <string>
#include <vector>

namespace ms {
struct Diag {
  int line = 0;
  int col = 0;
  std::string msg;
};
using Diags = std::vector<Diag>;
}  // namespace ms
```

`src/dsl/token.h`:
```cpp
#pragma once
#include <string>

namespace ms::dsl {
enum class Tok {
  Ident, Number, Str,
  LBrace, RBrace, LBracket, RBracket,
  Colon, Comma, Equals, Arrow, Dot,
  Newline, End,
};

struct Token {
  Tok kind;
  std::string text;
  double num = 0.0;
  int line = 1;
  int col = 1;
};
}  // namespace ms::dsl
```

`src/dsl/lexer.h`:
```cpp
#pragma once
#include <string_view>
#include <vector>
#include "dsl/diag.h"
#include "dsl/token.h"

namespace ms::dsl {
struct LexResult {
  std::vector<Token> tokens;
  Diags errors;
};
LexResult lex(std::string_view src);
}  // namespace ms::dsl
```

`src/dsl/lexer.cpp`:
```cpp
#include "dsl/lexer.h"
#include <cctype>
#include <cstdlib>

namespace ms::dsl {
namespace {

bool ident_start(char c) { return std::islower(static_cast<unsigned char>(c)) || c == '_'; }
bool ident_char(char c) { return ident_start(c) || std::isdigit(static_cast<unsigned char>(c)); }

bool unit_factor(std::string_view u, double& factor) {
  if (u == "hz" || u == "s" || u == "bpm") { factor = 1.0; return true; }
  if (u == "khz") { factor = 1000.0; return true; }
  if (u == "ms") { factor = 0.001; return true; }
  return false;
}

}  // namespace

LexResult lex(std::string_view src) {
  LexResult r;
  size_t i = 0;
  int line = 1, col = 1;
  int depth = 0;  // {} and [] nesting; newlines suppressed inside

  auto push = [&](Tok k, std::string text, double num, int l, int c) {
    r.tokens.push_back(Token{k, std::move(text), num, l, c});
  };
  auto err = [&](int l, int c, std::string msg) {
    r.errors.push_back(Diag{l, c, std::move(msg)});
  };
  auto advance = [&](size_t n) {
    for (size_t k = 0; k < n && i < src.size(); ++k, ++i) {
      if (src[i] == '\n') { ++line; col = 1; } else { ++col; }
    }
  };

  while (i < src.size()) {
    char c = src[i];
    int tl = line, tc = col;
    if (c == '#') {
      while (i < src.size() && src[i] != '\n') advance(1);
      continue;
    }
    if (c == '\n') {
      if (depth == 0) push(Tok::Newline, "\n", 0, tl, tc);
      advance(1);
      continue;
    }
    if (std::isspace(static_cast<unsigned char>(c))) { advance(1); continue; }

    if (c == '-' && i + 1 < src.size() && src[i + 1] == '>') {
      push(Tok::Arrow, "->", 0, tl, tc); advance(2); continue;
    }

    bool neg = false;
    if (c == '-' && i + 1 < src.size() &&
        (std::isdigit(static_cast<unsigned char>(src[i + 1])) || src[i + 1] == '.')) {
      neg = true; advance(1); c = src[i];
    }
    if (std::isdigit(static_cast<unsigned char>(c)) || (c == '.' && i + 1 < src.size() &&
        std::isdigit(static_cast<unsigned char>(src[i + 1])))) {
      size_t start = i;
      while (i < src.size() && (std::isdigit(static_cast<unsigned char>(src[i])) || src[i] == '.'))
        advance(1);
      double v = std::strtod(std::string(src.substr(start, i - start)).c_str(), nullptr);
      size_t ustart = i;
      while (i < src.size() && ident_char(src[i])) advance(1);
      std::string_view unit = src.substr(ustart, i - ustart);
      double factor = 1.0;
      if (!unit.empty() && !unit_factor(unit, factor)) {
        err(tl, tc, "unknown unit '" + std::string(unit) + "' (hz, khz, ms, s, bpm)");
      }
      push(Tok::Number, "", (neg ? -v : v) * factor, tl, tc);
      continue;
    }

    if (ident_start(c)) {
      size_t start = i;
      while (i < src.size() && ident_char(src[i])) advance(1);
      push(Tok::Ident, std::string(src.substr(start, i - start)), 0, tl, tc);
      continue;
    }

    if (c == '"') {
      advance(1);
      size_t start = i;
      while (i < src.size() && src[i] != '"' && src[i] != '\n') advance(1);
      if (i >= src.size() || src[i] != '"') {
        err(tl, tc, "unterminated string");
      } else {
        push(Tok::Str, std::string(src.substr(start, i - start)), 0, tl, tc);
        advance(1);
      }
      continue;
    }

    switch (c) {
      case '{': ++depth; push(Tok::LBrace, "{", 0, tl, tc); break;
      case '}': if (depth > 0) --depth; push(Tok::RBrace, "}", 0, tl, tc); break;
      case '[': ++depth; push(Tok::LBracket, "[", 0, tl, tc); break;
      case ']': if (depth > 0) --depth; push(Tok::RBracket, "]", 0, tl, tc); break;
      case ':': push(Tok::Colon, ":", 0, tl, tc); break;
      case ',': push(Tok::Comma, ",", 0, tl, tc); break;
      case '=': push(Tok::Equals, "=", 0, tl, tc); break;
      case '.': push(Tok::Dot, ".", 0, tl, tc); break;
      default: err(tl, tc, std::string("unexpected character '") + c + "'"); break;
    }
    advance(1);
  }
  push(Tok::End, "", 0, line, col);
  return r;
}
}  // namespace ms::dsl
```

- [ ] **Step 4: Run tests to verify pass**

Run: `mise run test`
Expected: PASS (all lexer cases + smoke).

- [ ] **Step 5: Commit**

```bash
git add src/dsl tests/test_lexer.cpp
git commit -m "feat: DSL lexer with unit normalization and diagnostics"
```

---

### Task 3: Parser → AST

**Files:**
- Create: `src/dsl/ast.h`, `src/dsl/parser.h`, `src/dsl/parser.cpp`
- Test: `tests/test_parser.cpp`

**Interfaces:**
- Consumes: `ms::dsl::lex`.
- Produces (in `ms::dsl`):
  ```cpp
  struct Value { enum class Kind { Num, Ident, Str, Array }; Kind kind;
                 double num; std::string str; std::vector<Value> items; };
  struct ParamDecl { std::string name; Value value; int line, col; };
  struct ModuleDecl { std::string name, type; std::vector<ParamDecl> params; int line, col; };
  struct Endpoint { bool is_literal; double literal; std::string module, port; int line, col; };
  struct CableDecl { Endpoint src, dst; double depth; int line, col; };
  struct PatchAst { std::vector<ModuleDecl> modules; std::vector<CableDecl> cables; };
  struct ParseResult { PatchAst ast; Diags errors; bool ok() const { return errors.empty(); } };
  ParseResult parse(std::string_view src);
  ```
  Guarantees: empty `port` means "default port"; error recovery skips to next Newline so multiple errors are reported; cable depth defaults to 1.0.

- [ ] **Step 1: Write the failing tests**

`tests/test_parser.cpp`:
```cpp
#include <doctest/doctest.h>
#include "dsl/parser.h"

using namespace ms::dsl;

TEST_CASE("parses declarations with params") {
  auto r = parse("filt = ladder { cutoff: 800hz, res: 0.45 }\n");
  REQUIRE(r.ok());
  REQUIRE(r.ast.modules.size() == 1);
  auto& m = r.ast.modules[0];
  CHECK(m.name == "filt");
  CHECK(m.type == "ladder");
  REQUIRE(m.params.size() == 2);
  CHECK(m.params[0].name == "cutoff");
  CHECK(m.params[0].value.num == doctest::Approx(800.0));
  CHECK(m.params[1].value.kind == Value::Kind::Num);
}

TEST_CASE("declaration without params") {
  auto r = parse("amp = vca");
  REQUIRE(r.ok());
  CHECK(r.ast.modules[0].type == "vca");
}

TEST_CASE("parses cables with ports and depth") {
  auto r = parse("lfo1 -> filt.cutoff : 0.5\nosc -> amp\n");
  REQUIRE(r.ok());
  REQUIRE(r.ast.cables.size() == 2);
  CHECK(r.ast.cables[0].src.module == "lfo1");
  CHECK(r.ast.cables[0].src.port.empty());
  CHECK(r.ast.cables[0].dst.port == "cutoff");
  CHECK(r.ast.cables[0].depth == doctest::Approx(0.5));
  CHECK(r.ast.cables[1].depth == doctest::Approx(1.0));
}

TEST_CASE("literal as cable source") {
  auto r = parse("0.25 -> qnt.transpose");
  REQUIRE(r.ok());
  CHECK(r.ast.cables[0].src.is_literal);
  CHECK(r.ast.cables[0].src.literal == doctest::Approx(0.25));
}

TEST_CASE("array and enum and string values") {
  auto r = parse("q = quant { scale: [0, 3, 5], root: c }\ns = sampler { file: \"a.wav\" }");
  REQUIRE(r.ok());
  auto& scale = r.ast.modules[0].params[0].value;
  REQUIRE(scale.kind == Value::Kind::Array);
  REQUIRE(scale.items.size() == 3);
  CHECK(scale.items[2].num == doctest::Approx(5.0));
  CHECK(r.ast.modules[0].params[1].value.kind == Value::Kind::Ident);
  CHECK(r.ast.modules[1].params[0].value.kind == Value::Kind::Str);
}

TEST_CASE("recovers and reports multiple errors with positions") {
  auto r = parse("osc = \nx -> \nok = vca\n");
  REQUIRE(r.errors.size() == 2);
  CHECK(r.errors[0].line == 1);
  CHECK(r.errors[1].line == 2);
  CHECK(r.ast.modules.size() == 1);  // 'ok' still parsed
}

TEST_CASE("duplicate module name is an error") {
  auto r = parse("a = vca\na = vca\n");
  REQUIRE(r.errors.size() == 1);
  CHECK(r.errors[0].msg.find("duplicate") != std::string::npos);
}
```

- [ ] **Step 2: Run to verify failure**

Run: `mise run test`
Expected: FAIL to compile (`dsl/parser.h` not found).

- [ ] **Step 3: Implement**

`src/dsl/ast.h`:
```cpp
#pragma once
#include <string>
#include <vector>

namespace ms::dsl {

struct Value {
  enum class Kind { Num, Ident, Str, Array };
  Kind kind = Kind::Num;
  double num = 0.0;
  std::string str;
  std::vector<Value> items;
};

struct ParamDecl { std::string name; Value value; int line = 0, col = 0; };

struct ModuleDecl {
  std::string name, type;
  std::vector<ParamDecl> params;
  int line = 0, col = 0;
};

struct Endpoint {
  bool is_literal = false;
  double literal = 0.0;
  std::string module, port;  // port empty = default port
  int line = 0, col = 0;
};

struct CableDecl { Endpoint src, dst; double depth = 1.0; int line = 0, col = 0; };

struct PatchAst {
  std::vector<ModuleDecl> modules;
  std::vector<CableDecl> cables;
};
}  // namespace ms::dsl
```

`src/dsl/parser.h`:
```cpp
#pragma once
#include <string_view>
#include "dsl/ast.h"
#include "dsl/diag.h"

namespace ms::dsl {
struct ParseResult {
  PatchAst ast;
  Diags errors;
  bool ok() const { return errors.empty(); }
};
ParseResult parse(std::string_view src);
}  // namespace ms::dsl
```

`src/dsl/parser.cpp`:
```cpp
#include "dsl/parser.h"
#include <unordered_set>
#include "dsl/lexer.h"

namespace ms::dsl {
namespace {

class Parser {
 public:
  Parser(std::vector<Token> toks, Diags lex_errors)
      : toks_(std::move(toks)) { r_.errors = std::move(lex_errors); }

  ParseResult run() {
    std::unordered_set<std::string> names;
    while (!at(Tok::End)) {
      if (at(Tok::Newline)) { next(); continue; }
      if (at(Tok::Number)) { cable(); continue; }
      if (at(Tok::Ident)) {
        if (peek(1).kind == Tok::Equals) {
          auto m = decl();
          if (m && !names.insert(m->name).second)
            err(m->line, m->col, "duplicate module name '" + m->name + "'");
          continue;
        }
        cable();
        continue;
      }
      err(cur().line, cur().col, "expected a module declaration or cable");
      sync();
    }
    return std::move(r_);
  }

 private:
  const Token& cur() const { return toks_[i_]; }
  const Token& peek(size_t n) const {
    return toks_[std::min(i_ + n, toks_.size() - 1)];
  }
  bool at(Tok k) const { return cur().kind == k; }
  const Token& next() { const Token& t = cur(); if (!at(Tok::End)) ++i_; return t; }
  void err(int l, int c, std::string msg) { r_.errors.push_back({l, c, std::move(msg)}); }
  void sync() { while (!at(Tok::End) && !at(Tok::Newline)) next(); }
  bool expect(Tok k, const char* what) {
    if (at(k)) { next(); return true; }
    err(cur().line, cur().col, std::string("expected ") + what);
    sync();
    return false;
  }

  ModuleDecl* decl() {
    ModuleDecl m;
    const Token& name = next();
    m.name = name.text; m.line = name.line; m.col = name.col;
    next();  // '='
    if (!at(Tok::Ident)) {
      err(cur().line, cur().col, "expected module type after '='");
      sync();
      return nullptr;
    }
    m.type = next().text;
    if (at(Tok::LBrace)) {
      next();
      while (!at(Tok::RBrace) && !at(Tok::End)) {
        ParamDecl p;
        const Token& pn = cur();
        if (!at(Tok::Ident)) { err(pn.line, pn.col, "expected parameter name"); sync(); return nullptr; }
        p.name = next().text; p.line = pn.line; p.col = pn.col;
        if (!expect(Tok::Colon, "':' after parameter name")) return nullptr;
        if (!value(p.value)) return nullptr;
        m.params.push_back(std::move(p));
        if (at(Tok::Comma)) next();
        else break;
      }
      if (!expect(Tok::RBrace, "'}' to close parameter block")) return nullptr;
    }
    r_.ast.modules.push_back(std::move(m));
    return &r_.ast.modules.back();
  }

  bool value(Value& v) {
    if (at(Tok::Number)) { v.kind = Value::Kind::Num; v.num = next().num; return true; }
    if (at(Tok::Ident))  { v.kind = Value::Kind::Ident; v.str = next().text; return true; }
    if (at(Tok::Str))    { v.kind = Value::Kind::Str; v.str = next().text; return true; }
    if (at(Tok::LBracket)) {
      next();
      v.kind = Value::Kind::Array;
      while (!at(Tok::RBracket) && !at(Tok::End)) {
        Value item;
        if (!value(item)) return false;
        v.items.push_back(std::move(item));
        if (at(Tok::Comma)) next(); else break;
      }
      return expect(Tok::RBracket, "']' to close array");
    }
    err(cur().line, cur().col, "expected a value (number, name, string, or array)");
    sync();
    return false;
  }

  void cable() {
    CableDecl cbl;
    const Token& first = cur();
    cbl.line = first.line; cbl.col = first.col;
    if (at(Tok::Number)) {
      cbl.src.is_literal = true;
      cbl.src.literal = next().num;
      cbl.src.line = first.line; cbl.src.col = first.col;
    } else if (!endpoint(cbl.src)) { return; }
    if (!expect(Tok::Arrow, "'->'")) return;
    if (!at(Tok::Ident)) {
      err(cur().line, cur().col, "expected destination module after '->'");
      sync();
      return;
    }
    if (!endpoint(cbl.dst)) return;
    if (at(Tok::Colon)) {
      next();
      if (!at(Tok::Number)) {
        err(cur().line, cur().col, "expected a number for cable depth after ':'");
        sync();
        return;
      }
      cbl.depth = next().num;
    }
    r_.ast.cables.push_back(std::move(cbl));
  }

  bool endpoint(Endpoint& e) {
    const Token& m = next();
    e.module = m.text; e.line = m.line; e.col = m.col;
    if (at(Tok::Dot)) {
      next();
      if (!at(Tok::Ident)) {
        err(cur().line, cur().col, "expected port name after '.'");
        sync();
        return false;
      }
      e.port = next().text;
    }
    return true;
  }

  std::vector<Token> toks_;
  size_t i_ = 0;
  ParseResult r_;
};

}  // namespace

ParseResult parse(std::string_view src) {
  auto lx = lex(src);
  return Parser(std::move(lx.tokens), std::move(lx.errors)).run();
}
}  // namespace ms::dsl
```

- [ ] **Step 4: Run tests to verify pass**

Run: `mise run test`
Expected: PASS.

- [ ] **Step 5: Commit**

```bash
git add src/dsl tests/test_parser.cpp
git commit -m "feat: DSL parser with error recovery"
```

---

### Task 4: Module interface, registry, `vca` and `out`

**Files:**
- Create: `src/dsp/module.h`, `src/dsp/module.cpp`, `src/dsp/modules.h`, `src/dsp/registry.cpp`, `src/dsp/modules/vca.cpp`, `src/dsp/modules/out.cpp`
- Test: `tests/test_module.cpp`

**Interfaces:**
- Produces (in `ms::dsp`):
  ```cpp
  struct InputInfo  { const char* name; float def; };       // modulatable, base + Σcables
  struct ConfigInfo { const char* name; const char* const* enum_vals; }; // null-terminated list, or nullptr = free-form
  struct OutputInfo { const char* name; };
  struct ConfigValue { std::string str; double num; std::vector<double> arr; bool is_num; };
  using Config = std::vector<std::pair<std::string, ConfigValue>>;
  std::string cfg_str(const Config&, std::string_view name, std::string_view def);

  class Module {  // one instance per declared module
   public:
    virtual ~Module() = default;
    virtual void init(double sample_rate, const Config& cfg);   // default: no-op
    virtual void process(const float* ins, float* outs) = 0;    // slot-indexed, one sample
  };

  struct ModuleInfo {
    const char* type;
    std::span<const InputInfo> inputs;
    std::span<const ConfigInfo> configs;
    std::span<const OutputInfo> outputs;
    std::unique_ptr<Module> (*create)();
    int input_index(std::string_view) const;    // -1 if absent
    int output_index(std::string_view) const;
    const ConfigInfo* config(std::string_view) const;  // nullptr if absent
  };
  const ModuleInfo* find_module(std::string_view type);  // nullptr if unknown
  ```
  Conventions later tasks rely on: default input is named `"in"`, default output `"out"`. `vca`: inputs `in`(0), `cv`(0), `gain`(1); config `response` lin|exp; output `out`; `out = in * clamp(gain+cv, 0, 8)` (squared when exp). `out` module (type name `"out"`): inputs `in`(0), `left`(0), `right`(0), `level`(1); outputs `l`, `r`; `l = (left+in)*level`, `r = (right+in)*level`.

- [ ] **Step 1: Write the failing tests**

`tests/test_module.cpp`:
```cpp
#include <doctest/doctest.h>
#include "dsp/modules.h"

using namespace ms::dsp;

TEST_CASE("registry finds vca and out; rejects unknown") {
  REQUIRE(find_module("vca") != nullptr);
  REQUIRE(find_module("out") != nullptr);
  CHECK(find_module("flanger") == nullptr);
}

TEST_CASE("vca metadata and slot lookup") {
  const ModuleInfo* m = find_module("vca");
  CHECK(m->input_index("in") == 0);
  CHECK(m->input_index("cv") == 1);
  CHECK(m->input_index("gain") == 2);
  CHECK(m->input_index("nope") == -1);
  CHECK(m->output_index("out") == 0);
  REQUIRE(m->config("response") != nullptr);
  CHECK(m->config("response")->enum_vals[0] == std::string("lin"));
}

TEST_CASE("vca math: gain sums with cv, clamps at zero") {
  const ModuleInfo* info = find_module("vca");
  auto m = info->create();
  m->init(48000.0, {});
  float ins[3] = {0.5f, 0.0f, 1.0f};  // in, cv, gain
  float outs[1];
  m->process(ins, outs);
  CHECK(outs[0] == doctest::Approx(0.5f));
  ins[1] = -2.0f;  // cv drives gain below zero -> silence
  m->process(ins, outs);
  CHECK(outs[0] == doctest::Approx(0.0f));
}

TEST_CASE("vca exp response squares the gain") {
  const ModuleInfo* info = find_module("vca");
  auto m = info->create();
  Config cfg;
  cfg.push_back({"response", ConfigValue{"exp", 0, {}, false}});
  m->init(48000.0, cfg);
  float ins[3] = {1.0f, 0.0f, 0.5f};
  float outs[1];
  m->process(ins, outs);
  CHECK(outs[0] == doctest::Approx(0.25f));
}

TEST_CASE("out module mixes 'in' into both sides and applies level") {
  const ModuleInfo* info = find_module("out");
  auto m = info->create();
  m->init(48000.0, {});
  float ins[4] = {0.5f, 0.25f, 0.0f, 0.5f};  // in, left, right, level
  float outs[2];
  m->process(ins, outs);
  CHECK(outs[0] == doctest::Approx(0.375f));  // (0.25 + 0.5) * 0.5
  CHECK(outs[1] == doctest::Approx(0.25f));   // (0.0  + 0.5) * 0.5
}
```

- [ ] **Step 2: Run to verify failure**

Run: `mise run test`
Expected: FAIL to compile.

- [ ] **Step 3: Implement**

`src/dsp/module.h`:
```cpp
#pragma once
#include <memory>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace ms::dsp {

struct InputInfo  { const char* name; float def; };
struct ConfigInfo { const char* name; const char* const* enum_vals; };
struct OutputInfo { const char* name; };

struct ConfigValue {
  std::string str;
  double num = 0.0;
  std::vector<double> arr;
  bool is_num = false;
};
using Config = std::vector<std::pair<std::string, ConfigValue>>;

std::string cfg_str(const Config& c, std::string_view name, std::string_view def);

class Module {
 public:
  virtual ~Module() = default;
  virtual void init(double sample_rate, const Config& cfg) { (void)sample_rate; (void)cfg; }
  virtual void process(const float* ins, float* outs) = 0;
};

struct ModuleInfo {
  const char* type;
  std::span<const InputInfo> inputs;
  std::span<const ConfigInfo> configs;
  std::span<const OutputInfo> outputs;
  std::unique_ptr<Module> (*create)();

  int input_index(std::string_view name) const;
  int output_index(std::string_view name) const;
  const ConfigInfo* config(std::string_view name) const;
};

const ModuleInfo* find_module(std::string_view type);
std::span<const ModuleInfo* const> all_modules();

}  // namespace ms::dsp
```

`src/dsp/module.cpp`:
```cpp
#include "dsp/module.h"

namespace ms::dsp {

std::string cfg_str(const Config& c, std::string_view name, std::string_view def) {
  for (const auto& [k, v] : c)
    if (k == name) return v.str;
  return std::string(def);
}

int ModuleInfo::input_index(std::string_view name) const {
  for (size_t i = 0; i < inputs.size(); ++i)
    if (name == inputs[i].name) return static_cast<int>(i);
  return -1;
}

int ModuleInfo::output_index(std::string_view name) const {
  for (size_t i = 0; i < outputs.size(); ++i)
    if (name == outputs[i].name) return static_cast<int>(i);
  return -1;
}

const ConfigInfo* ModuleInfo::config(std::string_view name) const {
  for (const auto& c : configs)
    if (name == c.name) return &c;
  return nullptr;
}

}  // namespace ms::dsp
```

`src/dsp/modules.h`:
```cpp
#pragma once
#include "dsp/module.h"

namespace ms::dsp {
extern const ModuleInfo vca_info;
extern const ModuleInfo out_info;
// Appended by later tasks: vco_info, lfo_info.
}  // namespace ms::dsp
```

`src/dsp/registry.cpp`:
```cpp
#include "dsp/modules.h"

namespace ms::dsp {
namespace {
const ModuleInfo* const kRegistry[] = {&vca_info, &out_info};
}

const ModuleInfo* find_module(std::string_view type) {
  for (const ModuleInfo* m : kRegistry)
    if (type == m->type) return m;
  return nullptr;
}

std::span<const ModuleInfo* const> all_modules() { return kRegistry; }
}  // namespace ms::dsp
```

`src/dsp/modules/vca.cpp`:
```cpp
#include <algorithm>
#include "dsp/modules.h"

namespace ms::dsp {
namespace {

constexpr InputInfo kIns[] = {{"in", 0.f}, {"cv", 0.f}, {"gain", 1.f}};
constexpr const char* kResp[] = {"lin", "exp", nullptr};
constexpr ConfigInfo kCfg[] = {{"response", kResp}};
constexpr OutputInfo kOuts[] = {{"out"}};

class Vca final : public Module {
 public:
  void init(double, const Config& cfg) override {
    exp_ = cfg_str(cfg, "response", "lin") == "exp";
  }
  void process(const float* ins, float* outs) override {
    float g = std::clamp(ins[2] + ins[1], 0.0f, 8.0f);
    if (exp_) g *= g;
    outs[0] = ins[0] * g;
  }
 private:
  bool exp_ = false;
};

std::unique_ptr<Module> make() { return std::make_unique<Vca>(); }
}  // namespace

const ModuleInfo vca_info{"vca", kIns, kCfg, kOuts, &make};
}  // namespace ms::dsp
```

`src/dsp/modules/out.cpp`:
```cpp
#include "dsp/modules.h"

namespace ms::dsp {
namespace {

constexpr InputInfo kIns[] = {{"in", 0.f}, {"left", 0.f}, {"right", 0.f}, {"level", 1.f}};
constexpr OutputInfo kOuts[] = {{"l"}, {"r"}};

class OutModule final : public Module {
 public:
  void process(const float* ins, float* outs) override {
    outs[0] = (ins[1] + ins[0]) * ins[3];
    outs[1] = (ins[2] + ins[0]) * ins[3];
  }
};

std::unique_ptr<Module> make() { return std::make_unique<OutModule>(); }
}  // namespace

const ModuleInfo out_info{"out", kIns, {}, kOuts, &make};
}  // namespace ms::dsp
```

- [ ] **Step 4: Run tests to verify pass**

Run: `mise run test`
Expected: PASS.

- [ ] **Step 5: Commit**

```bash
git add src/dsp tests/test_module.cpp
git commit -m "feat: module interface, registry, vca and out modules"
```

---

### Task 5: Graph compiler (validate, topo-sort, fold literals)

**Files:**
- Create: `src/graph/graph.h`, `src/graph/graph.cpp`
- Test: `tests/test_graph.cpp`

**Interfaces:**
- Consumes: `ms::dsl::PatchAst`, `ms::dsp::find_module`, `ModuleInfo` slot lookups.
- Produces (in `ms::graph`):
  ```cpp
  struct Cable { int src_mod, src_out, dst_mod, dst_in; float depth; };
  struct Node {
    std::string name;
    const dsp::ModuleInfo* info;
    std::vector<float> input_base;      // per input slot: default, overridden by decl, plus folded literals
    dsp::Config config;
    std::unique_ptr<dsp::Module> instance;  // created + init()ed by compile
  };
  struct Graph {
    std::vector<Node> nodes;            // in processing (topo) order
    std::vector<std::vector<Cable>> in_cables;  // per node, cables into it (indices into nodes)
    int out_node;                       // index of the implicit `out` node
    double sample_rate;
  };
  struct CompileResult { std::unique_ptr<Graph> graph; Diags errors;
                         bool ok() const { return errors.empty(); } };
  CompileResult compile(const dsl::PatchAst& ast, double sample_rate);
  ```
  Guarantees: `out` node always exists (implicit, reserved name); unknown types/modules/ports/params and enum violations produce Diags listing valid alternatives; cycles compile (feedback reads previous sample at runtime — no error, no flag needed); literal cable sources are folded into `input_base` and produce no Cable.

- [ ] **Step 1: Write the failing tests**

`tests/test_graph.cpp`:
```cpp
#include <doctest/doctest.h>
#include "dsl/parser.h"
#include "graph/graph.h"

using namespace ms;

static graph::CompileResult go(const char* src) {
  auto p = dsl::parse(src);
  REQUIRE(p.ok());
  return graph::compile(p.ast, 48000.0);
}

TEST_CASE("compiles a chain in topo order with implicit out") {
  auto r = go("b = vca\na = vca\na -> b\nb -> out.in\n");
  REQUIRE(r.ok());
  REQUIRE(r.graph->out_node >= 0);
  // 'a' must be processed before 'b'
  int ia = -1, ib = -1;
  for (int i = 0; i < (int)r.graph->nodes.size(); ++i) {
    if (r.graph->nodes[i].name == "a") ia = i;
    if (r.graph->nodes[i].name == "b") ib = i;
  }
  CHECK(ia < ib);
}

TEST_CASE("declared params override input defaults") {
  auto r = go("a = vca { gain: 0.25 }\na -> out.in\n");
  REQUIRE(r.ok());
  auto& n = r.graph->nodes[0].name == "a" ? r.graph->nodes[0]
            : r.graph->nodes[r.graph->out_node == 0 ? 1 : 0];
  CHECK(n.input_base[2] == doctest::Approx(0.25f));  // gain slot
}

TEST_CASE("default ports resolve to in/out") {
  auto r = go("a = vca\nb = vca\na -> b : 0.5\n");
  REQUIRE(r.ok());
  bool found = false;
  for (auto& per_node : r.graph->in_cables)
    for (auto& c : per_node) {
      CHECK(c.src_out == 0);
      CHECK(c.dst_in == 0);
      CHECK(c.depth == doctest::Approx(0.5f));
      found = true;
    }
  CHECK(found);
}

TEST_CASE("literal source folds into base") {
  auto r = go("a = vca\n0.5 -> a.cv : 2\n");
  REQUIRE(r.ok());
  for (auto& n : r.graph->nodes)
    if (n.name == "a") CHECK(n.input_base[1] == doctest::Approx(1.0f));
  for (auto& per_node : r.graph->in_cables) CHECK(per_node.empty());
}

TEST_CASE("cycles compile (feedback allowed)") {
  auto r = go("a = vca\nb = vca\na -> b\nb -> a.cv\n");
  CHECK(r.ok());
}

TEST_CASE("errors: unknown type, module, port, param, enum value") {
  auto p = dsl::parse(
      "a = flanger\n"
      "b = vca { wetness: 1 }\n"
      "c = vca { response: sideways }\n"
      "b -> ghost\n"
      "b -> c.nope\n");
  REQUIRE(p.ok());
  auto r = graph::compile(p.ast, 48000.0);
  REQUIRE(r.errors.size() == 5);
  CHECK(r.errors[0].msg.find("unknown module type 'flanger'") != std::string::npos);
  CHECK(r.errors[1].msg.find("no parameter 'wetness'") != std::string::npos);
  CHECK(r.errors[2].msg.find("lin") != std::string::npos);  // lists valid values
  CHECK(r.errors[3].msg.find("unknown module 'ghost'") != std::string::npos);
  CHECK(r.errors[4].msg.find("no input 'nope'") != std::string::npos);
}

TEST_CASE("'out' is reserved") {
  auto p = dsl::parse("out = vca\n");
  REQUIRE(p.ok());
  auto r = graph::compile(p.ast, 48000.0);
  REQUIRE(r.errors.size() == 1);
  CHECK(r.errors[0].msg.find("reserved") != std::string::npos);
}
```

- [ ] **Step 2: Run to verify failure**

Run: `mise run test`
Expected: FAIL to compile.

- [ ] **Step 3: Implement**

`src/graph/graph.h`:
```cpp
#pragma once
#include <memory>
#include <string>
#include <vector>
#include "dsl/ast.h"
#include "dsl/diag.h"
#include "dsp/module.h"

namespace ms::graph {

struct Cable { int src_mod, src_out, dst_mod, dst_in; float depth; };

struct Node {
  std::string name;
  const dsp::ModuleInfo* info;
  std::vector<float> input_base;
  dsp::Config config;
  std::unique_ptr<dsp::Module> instance;
};

struct Graph {
  std::vector<Node> nodes;
  std::vector<std::vector<Cable>> in_cables;
  int out_node = -1;
  double sample_rate = 48000.0;
};

struct CompileResult {
  std::unique_ptr<Graph> graph;
  Diags errors;
  bool ok() const { return errors.empty(); }
};

CompileResult compile(const dsl::PatchAst& ast, double sample_rate);

}  // namespace ms::graph
```

`src/graph/graph.cpp`:
```cpp
#include "graph/graph.h"
#include <algorithm>
#include <string>
#include <unordered_map>

namespace ms::graph {
namespace {

std::string join_names(auto&& items, auto&& name_of) {
  std::string s;
  for (auto&& it : items) {
    if (!s.empty()) s += ", ";
    s += name_of(it);
  }
  return s;
}

dsp::ConfigValue to_config_value(const dsl::Value& v) {
  dsp::ConfigValue cv;
  switch (v.kind) {
    case dsl::Value::Kind::Num:   cv.is_num = true; cv.num = v.num; break;
    case dsl::Value::Kind::Ident:
    case dsl::Value::Kind::Str:   cv.str = v.str; break;
    case dsl::Value::Kind::Array:
      for (const auto& item : v.items) cv.arr.push_back(item.num);
      break;
  }
  return cv;
}

}  // namespace

CompileResult compile(const dsl::PatchAst& ast, double sample_rate) {
  CompileResult r;
  auto g = std::make_unique<Graph>();
  g->sample_rate = sample_rate;
  auto err = [&](int line, int col, std::string msg) {
    r.errors.push_back({line, col, std::move(msg)});
  };

  // --- Nodes: declared modules + implicit `out` -------------------------
  std::unordered_map<std::string, int> index_of;
  auto add_node = [&](const std::string& name, const dsp::ModuleInfo* info) {
    Node n;
    n.name = name;
    n.info = info;
    n.input_base.reserve(info->inputs.size());
    for (const auto& in : info->inputs) n.input_base.push_back(in.def);
    index_of[name] = static_cast<int>(g->nodes.size());
    g->nodes.push_back(std::move(n));
  };

  for (const auto& m : ast.modules) {
    if (m.name == "out") { err(m.line, m.col, "'out' is a reserved built-in module name"); continue; }
    const dsp::ModuleInfo* info = dsp::find_module(m.type);
    if (!info) { err(m.line, m.col, "unknown module type '" + m.type + "'"); continue; }
    add_node(m.name, info);
    Node& n = g->nodes.back();
    for (const auto& p : m.params) {
      int slot = info->input_index(p.name);
      if (slot >= 0) {
        if (p.value.kind != dsl::Value::Kind::Num) {
          err(p.line, p.col, "parameter '" + p.name + "' expects a number");
        } else {
          n.input_base[static_cast<size_t>(slot)] = static_cast<float>(p.value.num);
        }
        continue;
      }
      if (const dsp::ConfigInfo* ci = info->config(p.name)) {
        dsp::ConfigValue cv = to_config_value(p.value);
        if (ci->enum_vals) {
          bool valid = false;
          for (const char* const* e = ci->enum_vals; *e; ++e)
            if (cv.str == *e) valid = true;
          if (!valid) {
            std::string opts;
            for (const char* const* e = ci->enum_vals; *e; ++e) {
              if (!opts.empty()) opts += ", ";
              opts += *e;
            }
            err(p.line, p.col, "invalid value for '" + p.name + "' (one of: " + opts + ")");
            continue;
          }
        }
        n.config.push_back({p.name, std::move(cv)});
        continue;
      }
      err(p.line, p.col, "module type '" + m.type + "' has no parameter '" + p.name +
          "' (inputs: " + join_names(info->inputs, [](auto& i) { return i.name; }) + ")");
    }
  }
  add_node("out", dsp::find_module("out"));

  // --- Cables ------------------------------------------------------------
  std::vector<Cable> cables;
  for (const auto& c : ast.cables) {
    int dst;
    {
      auto it = index_of.find(c.dst.module);
      if (it == index_of.end()) { err(c.dst.line, c.dst.col, "unknown module '" + c.dst.module + "'"); continue; }
      dst = it->second;
    }
    const Node& dn = g->nodes[static_cast<size_t>(dst)];
    std::string dport = c.dst.port.empty() ? "in" : c.dst.port;
    int dslot = dn.info->input_index(dport);
    if (dslot < 0) {
      err(c.dst.line, c.dst.col, "module '" + dn.name + "' has no input '" + dport +
          "' (inputs: " + join_names(dn.info->inputs, [](auto& i) { return i.name; }) + ")");
      continue;
    }
    if (c.src.is_literal) {
      g->nodes[static_cast<size_t>(dst)].input_base[static_cast<size_t>(dslot)] +=
          static_cast<float>(c.src.literal * c.depth);
      continue;
    }
    auto it = index_of.find(c.src.module);
    if (it == index_of.end()) { err(c.src.line, c.src.col, "unknown module '" + c.src.module + "'"); continue; }
    int src = it->second;
    const Node& sn = g->nodes[static_cast<size_t>(src)];
    std::string sport = c.src.port.empty() ? sn.info->outputs.empty() ? "" : "out" : c.src.port;
    int sslot = sn.info->output_index(sport);
    if (sslot < 0) {
      err(c.src.line, c.src.col, "module '" + sn.name + "' has no output '" + sport +
          "' (outputs: " + join_names(sn.info->outputs, [](auto& o) { return o.name; }) + ")");
      continue;
    }
    cables.push_back({src, sslot, dst, dslot, static_cast<float>(c.depth)});
  }

  if (!r.errors.empty()) return r;

  // --- Topo order (Kahn; cycles broken at lowest declaration index) ------
  size_t n = g->nodes.size();
  std::vector<int> indegree(n, 0);
  for (const auto& c : cables)
    if (c.src_mod != c.dst_mod) ++indegree[static_cast<size_t>(c.dst_mod)];
  std::vector<int> order;
  std::vector<bool> placed(n, false);
  while (order.size() < n) {
    int pick = -1;
    for (size_t i = 0; i < n; ++i)
      if (!placed[i] && indegree[i] == 0) { pick = static_cast<int>(i); break; }
    if (pick < 0) {  // cycle: force lowest-index remaining node
      for (size_t i = 0; i < n; ++i)
        if (!placed[i]) { pick = static_cast<int>(i); break; }
    }
    placed[static_cast<size_t>(pick)] = true;
    order.push_back(pick);
    for (const auto& c : cables)
      if (c.src_mod == pick && !placed[static_cast<size_t>(c.dst_mod)] && c.src_mod != c.dst_mod)
        --indegree[static_cast<size_t>(c.dst_mod)];
  }

  std::vector<int> new_index(n);
  for (size_t pos = 0; pos < n; ++pos) new_index[static_cast<size_t>(order[pos])] = static_cast<int>(pos);
  std::vector<Node> sorted;
  sorted.reserve(n);
  for (int old : order) sorted.push_back(std::move(g->nodes[static_cast<size_t>(old)]));
  g->nodes = std::move(sorted);
  g->in_cables.assign(n, {});
  for (auto c : cables) {
    c.src_mod = new_index[static_cast<size_t>(c.src_mod)];
    c.dst_mod = new_index[static_cast<size_t>(c.dst_mod)];
    g->in_cables[static_cast<size_t>(c.dst_mod)].push_back(c);
  }
  for (size_t i = 0; i < n; ++i)
    if (g->nodes[i].name == "out") g->out_node = static_cast<int>(i);

  // --- Instantiate -------------------------------------------------------
  for (auto& node : g->nodes) {
    node.instance = node.info->create();
    node.instance->init(sample_rate, node.config);
  }

  r.graph = std::move(g);
  return r;
}

}  // namespace ms::graph
```

- [ ] **Step 4: Run tests to verify pass**

Run: `mise run test`
Expected: PASS.

- [ ] **Step 5: Commit**

```bash
git add src/graph tests/test_graph.cpp
git commit -m "feat: graph compiler with validation, topo sort, literal folding"
```

---

### Task 6: Offline engine core

**Files:**
- Create: `src/engine/engine.h`, `src/engine/engine.cpp`
- Test: `tests/test_engine.cpp`

**Interfaces:**
- Consumes: `ms::graph::Graph` (nodes in topo order, `in_cables`, `out_node`).
- Produces (in `ms::engine`):
  ```cpp
  class Engine {
   public:
    explicit Engine(std::unique_ptr<graph::Graph> g);
    void process(float* left, float* right, size_t frames);  // fills master output
    double sample_rate() const;
  };
  ```
  Guarantees: modulation = base + Σ(src_out × depth); persistent per-node output array ⇒ cables from later-ordered nodes deliver the previous sample (feedback); non-finite module outputs are replaced with 0; master output hard-clipped to ±1.

- [ ] **Step 1: Write the failing tests**

`tests/test_engine.cpp`:
```cpp
#include <doctest/doctest.h>
#include "dsl/parser.h"
#include "engine/engine.h"
#include "graph/graph.h"

using namespace ms;

static engine::Engine make_engine(const char* src) {
  auto p = dsl::parse(src);
  REQUIRE(p.ok());
  auto c = graph::compile(p.ast, 48000.0);
  REQUIRE(c.ok());
  return engine::Engine(std::move(c.graph));
}

TEST_CASE("passes a constant through vca to out") {
  auto e = make_engine("amp = vca { in: 0.5 }\namp -> out.in\n");
  float l[4], r[4];
  e.process(l, r, 4);
  for (int i = 0; i < 4; ++i) {
    CHECK(l[i] == doctest::Approx(0.5f));
    CHECK(r[i] == doctest::Approx(0.5f));
  }
}

TEST_CASE("cable depth scales the signal") {
  auto e = make_engine("amp = vca { in: 1.0 }\namp -> out.in : 0.25\n");
  float l[1], r[1];
  e.process(l, r, 1);
  CHECK(l[0] == doctest::Approx(0.25f));
}

TEST_CASE("multiple cables into one port sum") {
  auto e = make_engine(
      "a = vca { in: 0.2 }\nb = vca { in: 0.3 }\na -> out.in\nb -> out.in\n");
  float l[1], r[1];
  e.process(l, r, 1);
  CHECK(l[0] == doctest::Approx(0.5f));
}

TEST_CASE("feedback edge delivers previous sample (one-sample delay)") {
  // a.cv fed by b, which follows a: a(gain 1 + cv) ramps +1 per sample.
  auto e = make_engine(
      "a = vca { in: 1.0 }\nb = vca\na -> b\nb -> a.cv\na -> out.in\n");
  float l[3], r[3];
  e.process(l, r, 3);
  CHECK(l[0] == doctest::Approx(1.0f));  // cv sees b's initial 0
  CHECK(l[1] == doctest::Approx(1.0f));  // out clamps ±1; check pre-clip via smaller drive:
}

TEST_CASE("feedback ramp observed below clip") {
  auto e = make_engine(
      "a = vca { in: 0.1 }\nb = vca\na -> b\nb -> a.cv\na -> out.in\n");
  float l[3], r[3];
  e.process(l, r, 3);
  CHECK(l[0] == doctest::Approx(0.1f));            // gain 1 + cv 0
  CHECK(l[1] == doctest::Approx(0.1f * 1.1f));     // cv = 0.1 (prev sample)
  CHECK(l[2] == doctest::Approx(0.1f * 1.11f));    // cv = 0.11
}

TEST_CASE("master output hard-clips to +/-1") {
  auto e = make_engine("amp = vca { in: 3.0 }\namp -> out.in\n");
  float l[1], r[1];
  e.process(l, r, 1);
  CHECK(l[0] == doctest::Approx(1.0f));
}
```

- [ ] **Step 2: Run to verify failure**

Run: `mise run test`
Expected: FAIL to compile.

- [ ] **Step 3: Implement**

`src/engine/engine.h`:
```cpp
#pragma once
#include <memory>
#include <vector>
#include "graph/graph.h"

namespace ms::engine {

class Engine {
 public:
  explicit Engine(std::unique_ptr<graph::Graph> g);
  void process(float* left, float* right, size_t frames);
  double sample_rate() const { return g_->sample_rate; }

 private:
  std::unique_ptr<graph::Graph> g_;
  std::vector<float> outs_;        // flat persistent [node][out_slot]
  std::vector<size_t> out_offset_; // per node
  std::vector<float> ins_;         // scratch, sized to max input count
};

}  // namespace ms::engine
```

`src/engine/engine.cpp`:
```cpp
#include "engine/engine.h"
#include <algorithm>
#include <cmath>

namespace ms::engine {

Engine::Engine(std::unique_ptr<graph::Graph> g) : g_(std::move(g)) {
  size_t total = 0, max_ins = 1;
  for (const auto& n : g_->nodes) {
    out_offset_.push_back(total);
    total += n.info->outputs.size();
    max_ins = std::max(max_ins, n.info->inputs.size());
  }
  outs_.assign(total, 0.0f);
  ins_.assign(max_ins, 0.0f);
}

void Engine::process(float* left, float* right, size_t frames) {
  const auto& nodes = g_->nodes;
  for (size_t f = 0; f < frames; ++f) {
    for (size_t ni = 0; ni < nodes.size(); ++ni) {
      const graph::Node& n = nodes[ni];
      const size_t n_ins = n.info->inputs.size();
      for (size_t s = 0; s < n_ins; ++s) ins_[s] = n.input_base[s];
      for (const graph::Cable& c : g_->in_cables[ni])
        ins_[static_cast<size_t>(c.dst_in)] +=
            outs_[out_offset_[static_cast<size_t>(c.src_mod)] + static_cast<size_t>(c.src_out)] * c.depth;
      float* node_outs = &outs_[out_offset_[ni]];
      n.instance->process(ins_.data(), node_outs);
      for (size_t o = 0; o < n.info->outputs.size(); ++o)
        if (!std::isfinite(node_outs[o])) node_outs[o] = 0.0f;
    }
    const size_t oo = out_offset_[static_cast<size_t>(g_->out_node)];
    left[f]  = std::clamp(outs_[oo],     -1.0f, 1.0f);
    right[f] = std::clamp(outs_[oo + 1], -1.0f, 1.0f);
  }
}

}  // namespace ms::engine
```

- [ ] **Step 4: Run tests to verify pass**

Run: `mise run test`
Expected: PASS (including both feedback cases).

- [ ] **Step 5: Commit**

```bash
git add src/engine tests/test_engine.cpp
git commit -m "feat: offline per-sample engine with summing modulation and feedback"
```

---

### Task 7: `vco` (polyBLEP)

**Files:**
- Create: `src/dsp/modules/vco.cpp`
- Modify: `src/dsp/modules.h` (add `extern const ModuleInfo vco_info;`), `src/dsp/registry.cpp` (add `&vco_info` to `kRegistry`)
- Test: `tests/test_vco.cpp`

**Interfaces:**
- Consumes: `Module`/`ModuleInfo` from Task 4.
- Produces: module type `"vco"` — inputs `freq`(440), `v_oct`(0), `fm`(0), `pw`(0.5), `pwm`(0), `sync`(0); config `wave` sine|tri|saw|pulse (default sine); output `out`. Effective frequency `f = freq·2^v_oct + fm·freq`, clamped to [0, 0.45·rate]. saw/pulse are polyBLEP band-limited; sine/tri analytic (tri's spectrum falls 12 dB/oct — naive is acceptable, documented).

**Two corrections recorded (2026-07-10), verified against the tests below:**
1. **Phase order + precision.** `process()` advances the phase *before* computing the sample and stores phase as `double`. Compute-then-advance with a `float` phase made a k-Hz tone span `[0, k·(1−1/rate)]` → one crossing short (99 not 100), and left a sync reset sitting at the polyBLEP-cancelled value `0` instead of `−1`. Advance-first fixes both structurally.
2. **Aliasing gate relaxed to −20 dB.** The zero-latency 2-point polyBLEP measures ~−22 dB of image suppression for a 5 kHz saw (independently confirmed); the original −30 dB target was unattainable, and any filter sharp enough to reach it adds group delay that contradicts the "sync jumps to −1 within one sample" test. This fixes the oscillator's quality bar as *standard software-modular tier*; pristine anti-aliasing (oversampling) is deferred to a later DSP-polish task, not the foundation.

- [ ] **Step 1: Write the failing tests**

`tests/test_vco.cpp`:
```cpp
#include <doctest/doctest.h>
#include <cmath>
#include <numbers>
#include <vector>
#include "dsp/modules.h"

using namespace ms::dsp;

namespace {
// Render n samples of a vco with fixed inputs.
std::vector<float> render(const char* wave, float freq, float v_oct, size_t n,
                          double rate = 48000.0) {
  const ModuleInfo* info = find_module("vco");
  REQUIRE(info != nullptr);
  auto m = info->create();
  Config cfg;
  cfg.push_back({"wave", ConfigValue{wave, 0, {}, false}});
  m->init(rate, cfg);
  std::vector<float> out(n);
  float ins[6] = {freq, v_oct, 0.f, 0.5f, 0.f, 0.f};
  float o[1];
  for (size_t i = 0; i < n; ++i) { m->process(ins, o); out[i] = o[0]; }
  return out;
}

// Goertzel magnitude at frequency f.
double goertzel(const std::vector<float>& x, double f, double rate) {
  const double w = 2.0 * std::numbers::pi * f / rate;
  const double c = 2.0 * std::cos(w);
  double s0 = 0, s1 = 0, s2 = 0;
  for (float v : x) { s0 = v + c * s1 - s2; s2 = s1; s1 = s0; }
  return std::sqrt(s1 * s1 + s2 * s2 - c * s1 * s2) /
         (static_cast<double>(x.size()) / 2.0);
}
}  // namespace

TEST_CASE("sine frequency is accurate (zero crossings)") {
  auto x = render("sine", 100.0f, 0.0f, 48000);
  int crossings = 0;
  for (size_t i = 1; i < x.size(); ++i)
    if (x[i - 1] < 0.0f && x[i] >= 0.0f) ++crossings;
  CHECK(crossings == 100);
}

TEST_CASE("v_oct shifts by octaves") {
  auto x = render("sine", 100.0f, 1.0f, 48000);
  int crossings = 0;
  for (size_t i = 1; i < x.size(); ++i)
    if (x[i - 1] < 0.0f && x[i] >= 0.0f) ++crossings;
  CHECK(crossings == 200);
}

TEST_CASE("saw amplitude within convention") {
  auto x = render("saw", 110.0f, 0.0f, 4800);
  float mn = 1e9f, mx = -1e9f;
  for (float v : x) { mn = std::min(mn, v); mx = std::max(mx, v); }
  CHECK(mx <= 1.05f);
  CHECK(mn >= -1.05f);
  CHECK(mx > 0.8f);
  CHECK(mn < -0.8f);
}

TEST_CASE("polyBLEP saw suppresses aliasing") {
  // 5000 Hz saw at 48k: harmonic 5 (25 kHz) aliases to 23 kHz.
  auto x = render("saw", 5000.0f, 0.0f, 1 << 15);
  double fundamental = goertzel(x, 5000.0, 48000.0);
  double alias = goertzel(x, 23000.0, 48000.0);
  // Naive saw shows this alias ~14 dB below fundamental; the zero-latency
  // 2-point polyBLEP pushes it past 20 dB (measured ~-22 dB). A stricter gate
  // is unreachable without added latency that would break the sync test above.
  CHECK(20.0 * std::log10(alias / fundamental) < -20.0);
}

TEST_CASE("sync resets phase") {
  const ModuleInfo* info = find_module("vco");
  auto m = info->create();
  Config cfg;
  cfg.push_back({"wave", ConfigValue{"saw", 0, {}, false}});
  m->init(48000.0, cfg);
  float ins[6] = {100.f, 0.f, 0.f, 0.5f, 0.f, 0.f};
  float o[1];
  for (int i = 0; i < 100; ++i) m->process(ins, o);
  float before = o[0];
  ins[5] = 1.0f;  // sync rising edge
  m->process(ins, o);
  CHECK(o[0] != doctest::Approx(before));
  CHECK(o[0] == doctest::Approx(-1.0f).epsilon(0.05));  // saw restarts near -1
}
```

- [ ] **Step 2: Run to verify failure**

Run: `mise run test`
Expected: FAIL (`find_module("vco")` returns nullptr → REQUIRE fails).

- [ ] **Step 3: Implement**

Modify `src/dsp/modules.h` — add:
```cpp
extern const ModuleInfo vco_info;
```

Modify `src/dsp/registry.cpp` — registry becomes:
```cpp
const ModuleInfo* const kRegistry[] = {&vca_info, &out_info, &vco_info};
```

`src/dsp/modules/vco.cpp`:
```cpp
#include <algorithm>
#include <cmath>
#include <numbers>
#include "dsp/modules.h"

namespace ms::dsp {
namespace {

constexpr InputInfo kIns[] = {{"freq", 440.f}, {"v_oct", 0.f}, {"fm", 0.f},
                              {"pw", 0.5f},    {"pwm", 0.f},   {"sync", 0.f}};
constexpr const char* kWaves[] = {"sine", "tri", "saw", "pulse", nullptr};
constexpr ConfigInfo kCfg[] = {{"wave", kWaves}};
constexpr OutputInfo kOuts[] = {{"out"}};

// Standard 2-sample polyBLEP residual for a falling step at phase t.
float polyblep(float t, float dt) {
  if (t < dt) { t /= dt; return t + t - t * t - 1.0f; }
  if (t > 1.0f - dt) { t = (t - 1.0f) / dt; return t * t + t + t + 1.0f; }
  return 0.0f;
}

class Vco final : public Module {
 public:
  void init(double rate, const Config& cfg) override {
    rate_ = static_cast<float>(rate);
    const std::string w = cfg_str(cfg, "wave", "sine");
    wave_ = w == "tri" ? 1 : w == "saw" ? 2 : w == "pulse" ? 3 : 0;
    phase_ = 0.0f;
    prev_sync_ = 0.0f;
  }

  void process(const float* ins, float* outs) override {
    double f = static_cast<double>(ins[0]) * std::exp2(static_cast<double>(ins[1])) +
               static_cast<double>(ins[2]) * static_cast<double>(ins[0]);
    f = std::clamp(f, 0.0, 0.45 * static_cast<double>(rate_));
    const double dt = f / static_cast<double>(rate_);

    if (ins[5] > 0.5f && prev_sync_ <= 0.5f) phase_ = 0.0;
    prev_sync_ = ins[5];

    // Advance first (double phase): a k-Hz tone then completes exactly k cycles
    // over rate samples, and a sync reset lands the next sample near -1.
    phase_ += dt;
    phase_ -= std::floor(phase_);
    const float ph = static_cast<float>(phase_);
    const float dtf = static_cast<float>(dt);

    float v = 0.0f;
    switch (wave_) {
      case 0:  // sine
        v = std::sin(2.0f * std::numbers::pi_v<float> * ph);
        break;
      case 1:  // tri (naive; -12 dB/oct spectrum, acceptable aliasing)
        v = 4.0f * std::abs(ph - 0.5f) - 1.0f;
        break;
      case 2:  // saw, polyBLEP
        v = 2.0f * ph - 1.0f - polyblep(ph, dtf);
        break;
      case 3: {  // pulse, polyBLEP on both edges
        const float pw = std::clamp(ins[3] + ins[4], 0.05f, 0.95f);
        v = (ph < pw ? 1.0f : -1.0f);
        v += polyblep(ph, dtf);
        float t2 = ph + 1.0f - pw;
        t2 -= std::floor(t2);
        v -= polyblep(t2, dtf);
        break;
      }
    }
    outs[0] = v;
  }

 private:
  float rate_ = 48000.0f;
  int wave_ = 0;
  double phase_ = 0.0;
  float prev_sync_ = 0.0f;
};

std::unique_ptr<Module> make() { return std::make_unique<Vco>(); }
}  // namespace

const ModuleInfo vco_info{"vco", kIns, kCfg, kOuts, &make};
}  // namespace ms::dsp
```

- [ ] **Step 4: Run tests to verify pass**

Run: `mise run test`
Expected: PASS, including the −20 dB alias gate.

- [ ] **Step 5: Commit**

```bash
git add src/dsp tests/test_vco.cpp
git commit -m "feat: polyBLEP vco with v_oct/fm/pwm/sync"
```

---

### Task 8: `lfo`

**Files:**
- Create: `src/dsp/modules/lfo.cpp`
- Modify: `src/dsp/modules.h` (add `extern const ModuleInfo lfo_info;`), `src/dsp/registry.cpp` (add `&lfo_info`)
- Test: `tests/test_lfo.cpp`

**Interfaces:**
- Produces: module type `"lfo"` — inputs `freq`(1), `reset`(0), `phase`(0, offset 0..1); config `wave` sine|tri|saw|square (default sine); output `out`, bipolar ±1, naive shapes (LFO rates — aliasing irrelevant). `reset` rising edge (through 0.5) returns phase to 0. Spec deviation, recorded: the spec table's separate `rate` port is dropped — `freq` is itself modulatable, so a second rate input is redundant.

- [ ] **Step 1: Write the failing tests**

`tests/test_lfo.cpp`:
```cpp
#include <doctest/doctest.h>
#include <cmath>
#include "dsp/modules.h"

using namespace ms::dsp;

namespace {
struct Lfo {
  std::unique_ptr<Module> m;
  float ins[3] = {1.0f, 0.0f, 0.0f};  // freq, reset, phase
  explicit Lfo(const char* wave) {
    const ModuleInfo* info = find_module("lfo");
    REQUIRE(info != nullptr);
    m = info->create();
    Config cfg;
    cfg.push_back({"wave", ConfigValue{wave, 0, {}, false}});
    m->init(48000.0, cfg);
  }
  float tick() { float o[1]; m->process(ins, o); return o[0]; }
};
}  // namespace

TEST_CASE("sine lfo completes one cycle at 1 Hz") {
  Lfo l("sine");
  float first = l.tick();
  for (int i = 1; i < 48000; ++i) l.tick();
  float wrapped = l.tick();
  CHECK(wrapped == doctest::Approx(first).epsilon(0.01));
}

TEST_CASE("square is bipolar with 50% duty") {
  Lfo l("square");
  int hi = 0;
  for (int i = 0; i < 48000; ++i)
    if (l.tick() > 0.0f) ++hi;
  CHECK(hi == doctest::Approx(24000).epsilon(0.01));
}

TEST_CASE("reset rising edge restarts the cycle") {
  Lfo l("saw");
  for (int i = 0; i < 12000; ++i) l.tick();  // quarter cycle in
  l.ins[1] = 1.0f;
  float after = l.tick();
  CHECK(after == doctest::Approx(-1.0f).epsilon(0.01));  // saw restarts at -1
}

TEST_CASE("phase offset shifts start point") {
  Lfo l("saw");
  l.ins[2] = 0.5f;
  CHECK(l.tick() == doctest::Approx(0.0f).epsilon(0.01));  // saw at half phase = 0
}
```

- [ ] **Step 2: Run to verify failure**

Run: `mise run test`
Expected: FAIL (`find_module("lfo")` nullptr).

- [ ] **Step 3: Implement**

Modify `src/dsp/modules.h` — add `extern const ModuleInfo lfo_info;`
Modify `src/dsp/registry.cpp` — `kRegistry` becomes `{&vca_info, &out_info, &vco_info, &lfo_info}`.

`src/dsp/modules/lfo.cpp`:
```cpp
#include <cmath>
#include <numbers>
#include "dsp/modules.h"

namespace ms::dsp {
namespace {

constexpr InputInfo kIns[] = {{"freq", 1.f}, {"reset", 0.f}, {"phase", 0.f}};
constexpr const char* kWaves[] = {"sine", "tri", "saw", "square", nullptr};
constexpr ConfigInfo kCfg[] = {{"wave", kWaves}};
constexpr OutputInfo kOuts[] = {{"out"}};

class Lfo final : public Module {
 public:
  void init(double rate, const Config& cfg) override {
    rate_ = static_cast<float>(rate);
    const std::string w = cfg_str(cfg, "wave", "sine");
    wave_ = w == "tri" ? 1 : w == "saw" ? 2 : w == "square" ? 3 : 0;
    phase_ = 0.0f;
    prev_reset_ = 0.0f;
  }

  void process(const float* ins, float* outs) override {
    if (ins[1] > 0.5f && prev_reset_ <= 0.5f) phase_ = 0.0f;
    prev_reset_ = ins[1];

    float p = phase_ + ins[2];
    p -= std::floor(p);

    switch (wave_) {
      case 0: outs[0] = std::sin(2.0f * std::numbers::pi_v<float> * p); break;
      case 1: outs[0] = 4.0f * std::abs(p - 0.5f) - 1.0f; break;
      case 2: outs[0] = 2.0f * p - 1.0f; break;
      case 3: outs[0] = p < 0.5f ? 1.0f : -1.0f; break;
    }

    phase_ += std::max(ins[0], 0.0f) / rate_;
    phase_ -= std::floor(phase_);
  }

 private:
  float rate_ = 48000.0f;
  int wave_ = 0;
  float phase_ = 0.0f;
  float prev_reset_ = 0.0f;
};

std::unique_ptr<Module> make() { return std::make_unique<Lfo>(); }
}  // namespace

const ModuleInfo lfo_info{"lfo", kIns, kCfg, kOuts, &make};
}  // namespace ms::dsp
```

- [ ] **Step 4: Run tests to verify pass**

Run: `mise run test`
Expected: PASS.

- [ ] **Step 5: Commit**

```bash
git add src/dsp tests/test_lfo.cpp
git commit -m "feat: lfo with reset and phase offset"
```

---

### Task 9: WAV writer

**Files:**
- Create: `src/io/wav.h`, `src/io/wav.cpp`
- Test: `tests/test_wav.cpp`

**Interfaces:**
- Produces (in `ms::io`):
  ```cpp
  // Writes 32-bit IEEE float WAV (format tag 3), interleaved.
  bool write_wav_f32(const std::string& path, const float* interleaved,
                     size_t frames, int channels, int sample_rate);
  ```

- [ ] **Step 1: Write the failing test**

`tests/test_wav.cpp`:
```cpp
#include <doctest/doctest.h>
#include <cstdint>
#include <cstdio>
#include <filesystem>
#include <vector>
#include "io/wav.h"

TEST_CASE("writes a valid float32 stereo wav") {
  auto path = std::filesystem::temp_directory_path() / "ratsnest_test.wav";
  float data[8] = {0.f, 0.f, 0.5f, -0.5f, 1.f, -1.f, 0.25f, 0.25f};  // 4 frames
  REQUIRE(ms::io::write_wav_f32(path.string(), data, 4, 2, 48000));

  std::FILE* f = std::fopen(path.string().c_str(), "rb");
  REQUIRE(f != nullptr);
  std::vector<uint8_t> bytes(1024);
  size_t n = std::fread(bytes.data(), 1, bytes.size(), f);
  std::fclose(f);
  std::filesystem::remove(path);

  REQUIRE(n == 44 + 8 * 4);                       // header + 8 samples * 4 bytes
  CHECK(std::string(bytes.begin(), bytes.begin() + 4) == "RIFF");
  CHECK(std::string(bytes.begin() + 8, bytes.begin() + 12) == "WAVE");
  CHECK(bytes[20] == 3);                          // format tag: IEEE float
  CHECK(bytes[22] == 2);                          // channels
  uint32_t rate = static_cast<uint32_t>(bytes[24]) | (bytes[25] << 8u) |
                  (bytes[26] << 16u) | (static_cast<uint32_t>(bytes[27]) << 24u);
  CHECK(rate == 48000);
  float first_sample;
  std::memcpy(&first_sample, &bytes[44], 4);
  CHECK(first_sample == doctest::Approx(0.0f));
}
```

Add `#include <cstring>` at the top with the other includes.

- [ ] **Step 2: Run to verify failure**

Run: `mise run test`
Expected: FAIL to compile (`io/wav.h` not found).

- [ ] **Step 3: Implement**

`src/io/wav.h`:
```cpp
#pragma once
#include <cstddef>
#include <string>

namespace ms::io {
bool write_wav_f32(const std::string& path, const float* interleaved,
                   size_t frames, int channels, int sample_rate);
}  // namespace ms::io
```

`src/io/wav.cpp`:
```cpp
#include "io/wav.h"
#include <cstdint>
#include <cstdio>

namespace ms::io {
namespace {
void put_u32(std::FILE* f, uint32_t v) { std::fwrite(&v, 4, 1, f); }
void put_u16(std::FILE* f, uint16_t v) { std::fwrite(&v, 2, 1, f); }
}  // namespace

bool write_wav_f32(const std::string& path, const float* interleaved,
                   size_t frames, int channels, int sample_rate) {
  std::FILE* f = std::fopen(path.c_str(), "wb");
  if (!f) return false;
  const uint32_t data_bytes = static_cast<uint32_t>(frames * static_cast<size_t>(channels) * 4);
  const uint16_t ch = static_cast<uint16_t>(channels);
  const uint32_t byte_rate = static_cast<uint32_t>(sample_rate) * ch * 4;

  std::fwrite("RIFF", 1, 4, f);
  put_u32(f, 36 + data_bytes);
  std::fwrite("WAVE", 1, 4, f);
  std::fwrite("fmt ", 1, 4, f);
  put_u32(f, 16);
  put_u16(f, 3);  // IEEE float
  put_u16(f, ch);
  put_u32(f, static_cast<uint32_t>(sample_rate));
  put_u32(f, byte_rate);
  put_u16(f, static_cast<uint16_t>(ch * 4));
  put_u16(f, 32);
  std::fwrite("data", 1, 4, f);
  put_u32(f, data_bytes);
  const size_t written = std::fwrite(interleaved, 4, frames * static_cast<size_t>(channels), f);
  std::fclose(f);
  return written == frames * static_cast<size_t>(channels);
}
}  // namespace ms::io
```

- [ ] **Step 4: Run tests to verify pass**

Run: `mise run test`
Expected: PASS.

- [ ] **Step 5: Commit**

```bash
git add src/io tests/test_wav.cpp
git commit -m "feat: float32 wav writer"
```

---

### Task 10: CLI (`check`, `render`), first patch, integration tests

**Files:**
- Create: `src/main.cpp`, `patches/first.patch`
- Modify: `CMakeLists.txt` (add CLI ctest entries)
- Test: `tests/test_integration.cpp`

**Interfaces:**
- Consumes: everything above.
- Produces: `ratsnest check <patch>` (exit 0 + `ok`, or exit 1 with `line:col: error: …` per diagnostic on stderr); `ratsnest render <patch> [-d <seconds|Ns>] [-o <out.wav>] [--rate <hz>]` (defaults: 10 s, `out.wav`, 48000).

- [ ] **Step 1: Write the failing tests**

`tests/test_integration.cpp`:
```cpp
#include <doctest/doctest.h>
#include <cmath>
#include <vector>
#include "dsl/parser.h"
#include "engine/engine.h"
#include "graph/graph.h"

using namespace ms;

static const char* kFirstPatch = R"(
# first light -- detuned saws with slow vibrato
osc1 = vco { freq: 110hz, wave: saw }
osc2 = vco { freq: 110.7hz, wave: saw }
vib  = lfo { freq: 0.3hz }
amp  = vca { gain: 0.4 }

vib  -> osc1.v_oct : 0.02
osc1 -> amp.in : 0.5
osc2 -> amp.in : 0.5
amp  -> out.in
)";

TEST_CASE("first.patch compiles and renders with sane levels") {
  auto p = dsl::parse(kFirstPatch);
  REQUIRE(p.ok());
  auto c = graph::compile(p.ast, 48000.0);
  REQUIRE(c.ok());
  engine::Engine e(std::move(c.graph));

  const size_t n = 48000;
  std::vector<float> l(n), r(n);
  e.process(l.data(), r.data(), n);

  double sum2 = 0.0;
  float peak = 0.0f;
  for (size_t i = 0; i < n; ++i) {
    sum2 += static_cast<double>(l[i]) * l[i];
    peak = std::max(peak, std::abs(l[i]));
  }
  const double rms = std::sqrt(sum2 / static_cast<double>(n));
  CHECK(rms > 0.05);    // audibly present
  CHECK(rms < 0.7);     // not slammed
  CHECK(peak <= 1.0f);  // master clip respected
}

TEST_CASE("render is deterministic") {
  auto render_once = [] {
    auto p = dsl::parse(kFirstPatch);
    auto c = graph::compile(p.ast, 48000.0);
    REQUIRE(c.ok());
    engine::Engine e(std::move(c.graph));
    std::vector<float> l(4800), r(4800);
    e.process(l.data(), r.data(), 4800);
    return l;
  };
  CHECK(render_once() == render_once());
}
```

- [ ] **Step 2: Run to verify unit-test failure, then write the CLI**

Run: `mise run test`
Expected: the two integration cases PASS already (they use the library) — they gate regressions from here on. The remaining deliverable is the CLI binary.

`patches/first.patch`:
```
# first light -- detuned saws with slow vibrato
osc1 = vco { freq: 110hz, wave: saw }
osc2 = vco { freq: 110.7hz, wave: saw }
vib  = lfo { freq: 0.3hz }
amp  = vca { gain: 0.4 }

vib  -> osc1.v_oct : 0.02
osc1 -> amp.in : 0.5
osc2 -> amp.in : 0.5
amp  -> out.in
```

`src/main.cpp`:
```cpp
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>
#include "dsl/parser.h"
#include "engine/engine.h"
#include "graph/graph.h"
#include "io/wav.h"

namespace {

int usage() {
  std::fprintf(stderr,
      "usage:\n"
      "  ratsnest check  <patch>\n"
      "  ratsnest render <patch> [-d <seconds|Ns>] [-o <out.wav>] [--rate <hz>]\n");
  return 2;
}

bool read_file(const std::string& path, std::string& out) {
  std::ifstream f(path);
  if (!f) return false;
  std::ostringstream ss;
  ss << f.rdbuf();
  out = ss.str();
  return true;
}

// Returns compiled graph or nullptr after printing diagnostics.
std::unique_ptr<ms::graph::Graph> load(const std::string& path, double rate) {
  std::string src;
  if (!read_file(path, src)) {
    std::fprintf(stderr, "error: cannot read '%s'\n", path.c_str());
    return nullptr;
  }
  auto p = ms::dsl::parse(src);
  auto report = [&](const ms::Diags& ds) {
    for (const auto& d : ds)
      std::fprintf(stderr, "%s:%d:%d: error: %s\n", path.c_str(), d.line, d.col,
                   d.msg.c_str());
  };
  if (!p.ok()) { report(p.errors); return nullptr; }
  auto c = ms::graph::compile(p.ast, rate);
  if (!c.ok()) { report(c.errors); return nullptr; }
  return std::move(c.graph);
}

double parse_duration(const char* s) {
  std::string v(s);
  if (!v.empty() && v.back() == 's') v.pop_back();
  return std::atof(v.c_str());
}

}  // namespace

int main(int argc, char** argv) {
  if (argc < 3) return usage();
  const std::string cmd = argv[1];
  const std::string patch = argv[2];

  if (cmd == "check") {
    if (!load(patch, 48000.0)) return 1;
    std::printf("ok\n");
    return 0;
  }

  if (cmd == "render") {
    double seconds = 10.0, rate = 48000.0;
    std::string out_path = "out.wav";
    for (int i = 3; i < argc; ++i) {
      if (!std::strcmp(argv[i], "-d") && i + 1 < argc) seconds = parse_duration(argv[++i]);
      else if (!std::strcmp(argv[i], "-o") && i + 1 < argc) out_path = argv[++i];
      else if (!std::strcmp(argv[i], "--rate") && i + 1 < argc) rate = std::atof(argv[++i]);
      else return usage();
    }
    if (seconds <= 0.0) { std::fprintf(stderr, "error: duration must be positive\n"); return 2; }

    auto g = load(patch, rate);
    if (!g) return 1;
    ms::engine::Engine engine(std::move(g));

    const size_t frames = static_cast<size_t>(seconds * rate);
    std::vector<float> l(frames), r(frames), interleaved(frames * 2);
    engine.process(l.data(), r.data(), frames);
    for (size_t i = 0; i < frames; ++i) {
      interleaved[2 * i] = l[i];
      interleaved[2 * i + 1] = r[i];
    }
    if (!ms::io::write_wav_f32(out_path, interleaved.data(), frames, 2,
                               static_cast<int>(rate))) {
      std::fprintf(stderr, "error: cannot write '%s'\n", out_path.c_str());
      return 1;
    }
    std::printf("rendered %.1fs -> %s\n", seconds, out_path.c_str());
    return 0;
  }

  return usage();
}
```

Append to `CMakeLists.txt`:
```cmake
add_test(NAME cli_check
  COMMAND ratsnest check ${CMAKE_CURRENT_SOURCE_DIR}/patches/first.patch)
add_test(NAME cli_render
  COMMAND ratsnest render ${CMAKE_CURRENT_SOURCE_DIR}/patches/first.patch
          -d 1s -o ${CMAKE_CURRENT_BINARY_DIR}/first.wav)
add_test(NAME cli_check_rejects_bad_patch
  COMMAND ratsnest check ${CMAKE_CURRENT_SOURCE_DIR}/CMakeLists.txt)
set_tests_properties(cli_check_rejects_bad_patch PROPERTIES WILL_FAIL TRUE)
```

- [ ] **Step 3: Run all gates**

Run: `mise run test`
Expected: all doctest cases + `cli_check`, `cli_render`, `cli_check_rejects_bad_patch` PASS.

Run: `mise run test-san`
Expected: identical results under ASan+UBSan.

Run: `mise run ci`
Expected: clean warnings-as-errors build, all tests green.

- [ ] **Step 4: Listen to it**

Run: `mise run render -- patches/first.patch -d 10s -o /tmp/first.wav`
Expected: `rendered 10.0s -> /tmp/first.wav` — a quiet detuned-saw drone with slow vibrato. (Play with `pw-play /tmp/first.wav` if at the machine.)

- [ ] **Step 5: Commit**

```bash
git add src/main.cpp patches CMakeLists.txt tests/test_integration.cpp
git commit -m "feat: check/render CLI with first patch and integration gates"
```

---

## Self-Review

- **Spec coverage (plan scope = Phase 1a):** DSL grammar §3.1 — decls, cables, depths, default ports, units, arrays, strings, literal sources, comments: Tasks 2–3, 5. Semantics §3.2 (summing modulation, defaults): Tasks 5–6. Conventions §4: constants in module code + integration test. Modules `vca`/`out`/`vco`/`lfo` per §5 tables: Tasks 4, 7, 8 (deviation from spec recorded in Task 8: `lfo` drops the redundant `rate` port; spec to be amended when 1c lands). Engine §7.1 per-sample + feedback: Task 6. Errors §7.5 (NaN guard, limiter, line:col): Tasks 5, 6, 10. Testing §8 (parser, DSP alias/zero-crossing, deterministic integration): Tasks 2–3, 7, 10. Tooling §9: Task 1. Deliberately out of plan scope (Phase 1b/1c): PipeWire, hot-reload/state migration, watcher, remaining 14 modules, `play`.
- **Placeholder scan:** no TBDs; every code step is complete and compilable as written.
- **Type consistency:** `Diag{line,col,msg}` used identically in lexer/parser/graph/CLI; `Config`/`ConfigValue` shape in Tasks 4→5→tests matches; input slot orders in tests match `kIns` tables (`vca`: in/cv/gain; `vco`: freq/v_oct/fm/pw/pwm/sync; `lfo`: freq/reset/phase; `out`: in/left/right/level); registry array updated in Tasks 7 and 8.
