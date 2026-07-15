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

find_package(PkgConfig REQUIRED)
pkg_check_modules(PIPEWIRE REQUIRED IMPORTED_TARGET libpipewire-0.3)
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

file(GLOB MSPATCHES CONFIGURE_DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/patches/*.patch)
foreach(patch ${MSPATCHES})
  get_filename_component(patch_name ${patch} NAME_WE)
  add_test(NAME cli_check_${patch_name} COMMAND ratsnest check ${patch})
endforeach()

add_test(NAME cli_render
  COMMAND ratsnest render ${CMAKE_CURRENT_SOURCE_DIR}/patches/first.patch
          -d 1s -o ${CMAKE_CURRENT_BINARY_DIR}/first.wav)
add_test(NAME cli_check_rejects_bad_patch
  COMMAND ratsnest check ${CMAKE_CURRENT_SOURCE_DIR}/CMakeLists.txt)
set_tests_properties(cli_check_rejects_bad_patch PROPERTIES WILL_FAIL TRUE)
