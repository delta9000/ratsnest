cmake_minimum_required(VERSION 3.25)
project(ratsnest CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

option(RATSNEST_WERROR "Treat warnings as errors" OFF)
option(RATSNEST_SAN "Enable ASan + UBSan for every target" OFF)

add_compile_options(-Wall -Wextra -Wconversion)
# Never fuse a*b+c into an FMA: it rounds differently, so the same patch would
# render different samples depending on the CPU the build targets.
add_compile_options(-ffp-contract=off)
# SSE4.1 (any x86-64 CPU since ~2009) turns floor/round into one instruction
# instead of a branchy sequence; the results are the same bits.
if(CMAKE_SYSTEM_PROCESSOR MATCHES "x86_64|AMD64")
  add_compile_options(-march=x86-64-v2)
endif()
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

find_package(PkgConfig REQUIRED)
# 1.6: the MIDI input calls spa_ump_to_midi with conversion state, which
# older SPA headers do not have (1.4 lacks the state, 1.2 the whole header).
pkg_check_modules(PIPEWIRE REQUIRED IMPORTED_TARGET libpipewire-0.3>=1.6)
target_link_libraries(mscore PUBLIC PkgConfig::PIPEWIRE)

add_executable(ratsnest src/main.cpp)
target_link_libraries(ratsnest PRIVATE mscore)

include(FetchContent)
set(CMAKE_POLICY_VERSION_MINIMUM 3.5)
FetchContent_Declare(doctest
  URL https://github.com/doctest/doctest/archive/refs/tags/v2.4.11.tar.gz)
FetchContent_MakeAvailable(doctest)

enable_testing()
file(GLOB MSTEST_SOURCES CONFIGURE_DEPENDS tests/*.cpp)
add_executable(mstests ${MSTEST_SOURCES})
target_link_libraries(mstests PRIVATE mscore doctest::doctest)
add_test(NAME unit COMMAND mstests)

file(GLOB_RECURSE MSPATCHES CONFIGURE_DEPENDS
  ${CMAKE_CURRENT_SOURCE_DIR}/patches/*.patch
  ${CMAKE_CURRENT_SOURCE_DIR}/patches/*.sub)
foreach(patch ${MSPATCHES})
  file(RELATIVE_PATH patch_name ${CMAKE_CURRENT_SOURCE_DIR}/patches ${patch})
  string(REGEX REPLACE "\\.(patch|sub)$" "" patch_name ${patch_name})
  string(REPLACE "/" "_" patch_name ${patch_name})
  add_test(NAME cli_check_${patch_name} COMMAND ratsnest check ${patch})
endforeach()

add_test(NAME cli_render
  COMMAND ratsnest render ${CMAKE_CURRENT_SOURCE_DIR}/patches/first.patch
          -d 1s -o ${CMAKE_CURRENT_BINARY_DIR}/first.wav)
add_test(NAME cli_check_rejects_bad_patch
  COMMAND ratsnest check ${CMAKE_CURRENT_SOURCE_DIR}/CMakeLists.txt)
set_tests_properties(cli_check_rejects_bad_patch PROPERTIES WILL_FAIL TRUE)

add_test(NAME cli_describe COMMAND ratsnest describe additive)
set_tests_properties(cli_describe PROPERTIES PASS_REGULAR_EXPRESSION "number_array")
add_test(NAME cli_describe_unknown COMMAND ratsnest describe nonexistent)
set_tests_properties(cli_describe_unknown PROPERTIES WILL_FAIL TRUE)
add_test(NAME cli_describe_suggests COMMAND ratsnest describe vcoo)
set_tests_properties(cli_describe_suggests PROPERTIES PASS_REGULAR_EXPRESSION "did you mean 'vco'")

# Durations: 500ms is half a second (not 500 s), 2m is minutes, junk is an error.
add_test(NAME cli_render_ms
  COMMAND ratsnest render ${CMAKE_CURRENT_SOURCE_DIR}/patches/first.patch
          -d 500ms -o ${CMAKE_CURRENT_BINARY_DIR}/first-ms.wav)
set_tests_properties(cli_render_ms PROPERTIES PASS_REGULAR_EXPRESSION "rendered 0\\.5s")
add_test(NAME cli_render_bad_duration
  COMMAND ratsnest render ${CMAKE_CURRENT_SOURCE_DIR}/patches/first.patch
          -d 5sec -o ${CMAKE_CURRENT_BINARY_DIR}/first-bad.wav)
set_tests_properties(cli_render_bad_duration PROPERTIES WILL_FAIL TRUE)
# --rate takes a whole rate a device can run at: 0, negatives and junk are errors.
foreach(bad_rate 0 -5 1e9 44100.5 48k)
  add_test(NAME cli_render_bad_rate_${bad_rate}
    COMMAND ratsnest render ${CMAKE_CURRENT_SOURCE_DIR}/patches/first.patch
            -d 100ms --rate ${bad_rate} -o ${CMAKE_CURRENT_BINARY_DIR}/first-bad.wav)
  set_tests_properties(cli_render_bad_rate_${bad_rate} PROPERTIES
    PASS_REGULAR_EXPRESSION "error: --rate needs a whole sample rate")
endforeach()
add_test(NAME cli_render_unknown_option
  COMMAND ratsnest render ${CMAKE_CURRENT_SOURCE_DIR}/patches/first.patch --duration 5)
set_tests_properties(cli_render_unknown_option PROPERTIES
  PASS_REGULAR_EXPRESSION "unknown option '--duration'")
add_test(NAME cli_check_warns_silent
  COMMAND ratsnest check ${CMAKE_CURRENT_SOURCE_DIR}/tests/data/silent.patch)
set_tests_properties(cli_check_warns_silent PROPERTIES
  PASS_REGULAR_EXPRESSION "warning: nothing is cabled into 'out'")

# --tap: a take per endpoint beside the mix, with its own stats block; a tap
# that does not resolve is an error naming it.
add_test(NAME cli_render_tap
  COMMAND ratsnest render ${CMAKE_CURRENT_SOURCE_DIR}/patches/first.patch
          -d 1s -o ${CMAKE_CURRENT_BINARY_DIR}/first-tap.wav --tap saw=osc2)
set_tests_properties(cli_render_tap PROPERTIES
  PASS_REGULAR_EXPRESSION "tap saw = osc2 \\(mono\\) -> .*first-tap\\.saw\\.wav\npeak +-?[0-9]")
add_test(NAME cli_render_tap_unknown
  COMMAND ratsnest render ${CMAKE_CURRENT_SOURCE_DIR}/patches/first.patch
          -d 1s -o ${CMAKE_CURRENT_BINARY_DIR}/first-tap.wav --tap x=osc3)
set_tests_properties(cli_render_tap_unknown PROPERTIES
  PASS_REGULAR_EXPRESSION "error: --tap x=osc3: .*did you mean")
