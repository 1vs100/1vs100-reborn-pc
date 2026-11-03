cmake_minimum_required(VERSION 3.25)
project(onevs100_launcher LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

include(FetchContent)

set(SDL_SHARED OFF CACHE BOOL "" FORCE)
set(SDL_STATIC ON CACHE BOOL "" FORCE)
set(SDL_TEST_LIBRARY OFF CACHE BOOL "" FORCE)
set(SDL_AUDIO OFF CACHE BOOL "" FORCE)
set(SDL_CAMERA OFF CACHE BOOL "" FORCE)
set(SDL_JOYSTICK OFF CACHE BOOL "" FORCE)
set(SDL_HAPTIC OFF CACHE BOOL "" FORCE)
set(SDL_SENSOR OFF CACHE BOOL "" FORCE)
set(SDL_POWER OFF CACHE BOOL "" FORCE)
set(SDL_GPU OFF CACHE BOOL "" FORCE)

FetchContent_Declare(SDL3
  GIT_REPOSITORY https://github.com/libsdl-org/SDL.git
  GIT_TAG release-3.4.8
  GIT_SHALLOW TRUE)
FetchContent_Declare(imgui
  GIT_REPOSITORY https://github.com/ocornut/imgui.git
  GIT_TAG v1.91.9b
  GIT_SHALLOW TRUE)
FetchContent_Declare(tomlplusplus
  GIT_REPOSITORY https://github.com/marzer/tomlplusplus.git
  GIT_TAG v3.4.0
  GIT_SHALLOW TRUE)
FetchContent_MakeAvailable(SDL3 imgui tomlplusplus)

# Embed the UI font as a byte array.
set(FONT_FILE "${imgui_SOURCE_DIR}/misc/fonts/Roboto-Medium.ttf")
set(FONT_HEADER "${CMAKE_CURRENT_BINARY_DIR}/generated/font_data.h")
file(READ "${FONT_FILE}" font_hex HEX)
string(REGEX REPLACE "([0-9a-f][0-9a-f])" "0x\\1," font_bytes "${font_hex}")
file(WRITE "${FONT_HEADER}" "#pragma once\ninline constexpr unsigned char kFontData[] = {${font_bytes}};\n")

add_executable(onevs100-launcher WIN32
  main.cpp
  ${imgui_SOURCE_DIR}/imgui.cpp
  ${imgui_SOURCE_DIR}/imgui_draw.cpp
  ${imgui_SOURCE_DIR}/imgui_tables.cpp
  ${imgui_SOURCE_DIR}/imgui_widgets.cpp
  ${imgui_SOURCE_DIR}/backends/imgui_impl_sdl3.cpp
  ${imgui_SOURCE_DIR}/backends/imgui_impl_sdlrenderer3.cpp)
target_include_directories(onevs100-launcher PRIVATE
  ${imgui_SOURCE_DIR} ${imgui_SOURCE_DIR}/backends ${CMAKE_CURRENT_BINARY_DIR}/generated)
target_compile_definitions(onevs100-launcher PRIVATE IMGUI_DISABLE_DEMO_WINDOWS)
# toml++ errors are caught by value; MSVC/clang-cl needs unwind semantics
# or a corrupt onevs100.toml would terminate instead of showing the status.
if (MSVC)
  target_compile_options(onevs100-launcher PRIVATE /EHsc)
endif()
target_link_libraries(onevs100-launcher PRIVATE SDL3::SDL3-static tomlplusplus::tomlplusplus)
set_target_properties(onevs100-launcher PROPERTIES OUTPUT_NAME launcher)
