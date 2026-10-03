pkg_check_modules(SDL3 REQUIRED IMPORTED_TARGET sdl3)
find_package(OpenGL REQUIRED)
FetchContent_Declare(imgui
  URL https://github.com/ocornut/imgui/archive/f5befd2d29e66809cd1110a152e375a7f1981f06.tar.gz)
FetchContent_Declare(node_editor
  URL https://github.com/thedmd/imgui-node-editor/archive/021aa0ea4da13fed864bafb2a92d4c5205076866.tar.gz)
FetchContent_Declare(json
  URL https://github.com/nlohmann/json/archive/65ee68451d8eb2b5f3a30b410476ab83deb3289b.tar.gz)
FetchContent_MakeAvailable(imgui node_editor json)
add_library(studio_widgets STATIC
  ${imgui_SOURCE_DIR}/imgui.cpp ${imgui_SOURCE_DIR}/imgui_draw.cpp
  ${imgui_SOURCE_DIR}/imgui_tables.cpp ${imgui_SOURCE_DIR}/imgui_widgets.cpp
  ${imgui_SOURCE_DIR}/misc/cpp/imgui_stdlib.cpp
  ${imgui_SOURCE_DIR}/backends/imgui_impl_sdl3.cpp
  ${imgui_SOURCE_DIR}/backends/imgui_impl_opengl3.cpp
  ${node_editor_SOURCE_DIR}/imgui_node_editor.cpp
  ${node_editor_SOURCE_DIR}/imgui_node_editor_api.cpp
  ${node_editor_SOURCE_DIR}/imgui_canvas.cpp
  ${node_editor_SOURCE_DIR}/crude_json.cpp)
target_include_directories(studio_widgets SYSTEM PUBLIC
  ${imgui_SOURCE_DIR} ${imgui_SOURCE_DIR}/backends ${imgui_SOURCE_DIR}/misc/cpp
  ${node_editor_SOURCE_DIR})
target_link_libraries(studio_widgets PUBLIC PkgConfig::SDL3 OpenGL::GL)
# Upstream dependencies do not use this repository's warning policy.
target_compile_options(studio_widgets PRIVATE -w)
add_library(studio_session STATIC app/session.cpp app/bridge.cpp)
target_include_directories(studio_session PUBLIC app)
target_compile_definitions(studio_session PRIVATE RATSNEST_REFERENCE_PATH="${CMAKE_CURRENT_SOURCE_DIR}/docs/modules.md")
target_link_libraries(studio_session PUBLIC mscore nlohmann_json::nlohmann_json)
add_executable(ratsnest-studio app/studio.cpp)
target_link_libraries(ratsnest-studio PRIVATE studio_session studio_widgets)
add_executable(studio_tests app/test_session.cpp)
target_link_libraries(studio_tests PRIVATE studio_session doctest::doctest)
add_test(NAME studio_session COMMAND studio_tests)
find_package(Python3 COMPONENTS Interpreter REQUIRED)
add_test(NAME studio_mcp COMMAND ${Python3_EXECUTABLE}
  ${CMAKE_CURRENT_SOURCE_DIR}/app/test_mcp.py $<TARGET_FILE:ratsnest-studio>)
set_tests_properties(studio_mcp PROPERTIES TIMEOUT 30)
