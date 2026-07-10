cmake_minimum_required(VERSION 3.25)
project(modsynth CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

option(MODSYNTH_WERROR "Treat warnings as errors" OFF)
option(MODSYNTH_SAN "Enable ASan + UBSan for every target" OFF)

add_compile_options(-Wall -Wextra -Wconversion)
if(MODSYNTH_WERROR)
  add_compile_options(-Werror)
endif()
if(MODSYNTH_SAN)
  add_compile_options(-fsanitize=address,undefined -fno-omit-frame-pointer
                      -fno-sanitize-recover=all)
  add_link_options(-fsanitize=address,undefined)
endif()

file(GLOB_RECURSE MSCORE_SOURCES CONFIGURE_DEPENDS src/*.cpp)
list(REMOVE_ITEM MSCORE_SOURCES ${CMAKE_CURRENT_SOURCE_DIR}/src/main.cpp)
add_library(mscore STATIC ${MSCORE_SOURCES})
target_include_directories(mscore PUBLIC src)

add_executable(modsynth src/main.cpp)
target_link_libraries(modsynth PRIVATE mscore)

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
