cmake_minimum_required(VERSION 3.25)
project(onevs100_launcher LANGUAGES C CXX)

# Oldest macOS the .app claims; tools/mac/minimum_os.txt holds the one copy.
if(APPLE AND NOT CMAKE_OSX_DEPLOYMENT_TARGET)
  file(READ "${CMAKE_CURRENT_SOURCE_DIR}/../tools/mac/minimum_os.txt" _min_macos)
  string(STRIP "${_min_macos}" _min_macos)
  if(NOT _min_macos MATCHES "^[0-9]+\\.[0-9]+$")
    message(FATAL_ERROR "Bad version in tools/mac/minimum_os.txt: '${_min_macos}'")
  endif()
  # project() caches an empty target from the platform module, so plain CACHE
  # would not take; the guard above already keeps a user-set value.
  set(CMAKE_OSX_DEPLOYMENT_TARGET "${_min_macos}" CACHE STRING "Oldest supported macOS" FORCE)
endif()
set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
# The prebuilt Velopack library uses the static release CRT, so everything
# here does, in every configuration. This also leaves launcher.exe without a
# VC runtime DLL dependency.
set(CMAKE_MSVC_RUNTIME_LIBRARY MultiThreaded)

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
FetchContent_Declare(fmt
  GIT_REPOSITORY https://github.com/fmtlib/fmt.git
  GIT_TAG 12.1.0
  GIT_SHALLOW TRUE)
FetchContent_MakeAvailable(SDL3 imgui tomlplusplus fmt)

# Prebuilt C library for every desktop target; the Rust toolchain is not needed.
FetchContent_Declare(velopack
  URL https://github.com/velopack/velopack/releases/download/1.2.161/velopack_libc_1.2.161.zip
  URL_HASH SHA256=31de8f2cf05a47bd5328a9f98b0c41aec1582156f6075f1058b26941f7f17c20
  DOWNLOAD_EXTRACT_TIMESTAMP TRUE)
FetchContent_MakeAvailable(velopack)
string(TOLOWER "${CMAKE_SYSTEM_PROCESSOR}" _cpu)
if(_cpu MATCHES "^(x86_64|amd64|x64)$")
  set(_velopack_cpu x64)
elseif(_cpu MATCHES "^(aarch64|arm64)$")
  set(_velopack_cpu arm64)
else()
  message(FATAL_ERROR "No Velopack library for processor '${CMAKE_SYSTEM_PROCESSOR}'")
endif()
if(WIN32)
  set(_velopack_lib "${velopack_SOURCE_DIR}/lib-static/velopack_libc_win_${_velopack_cpu}_msvc.lib")
elseif(APPLE)
  set(_velopack_lib "${velopack_SOURCE_DIR}/lib-static/velopack_libc_osx_${_velopack_cpu}_gnu.a")
else()
  set(_velopack_lib "${velopack_SOURCE_DIR}/lib-static/velopack_libc_linux_${_velopack_cpu}_gnu.a")
endif()
add_library(velopack STATIC IMPORTED)
set_target_properties(velopack PROPERTIES
  IMPORTED_LOCATION "${_velopack_lib}"
  INTERFACE_INCLUDE_DIRECTORIES "${velopack_SOURCE_DIR}/include")

# The avatar format and preview renderer the game uses.
add_subdirectory(${CMAKE_CURRENT_SOURCE_DIR}/../native/src/avatar_native onevs100_avatar)

# The release version, compared against the update feed and shown in the footer.
file(READ "${CMAKE_CURRENT_SOURCE_DIR}/../VERSION" ONEVS100_RELEASE_VERSION)
string(STRIP "${ONEVS100_RELEASE_VERSION}" ONEVS100_RELEASE_VERSION)
if(NOT ONEVS100_RELEASE_VERSION MATCHES "^[0-9]+\\.[0-9]+\\.[0-9]+$")
  message(FATAL_ERROR "Bad version in VERSION: '${ONEVS100_RELEASE_VERSION}'")
endif()
set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/../VERSION")

# The footer shows the commit as <commit date>-<short hash>, read when CMake configures.
find_package(Git QUIET)
set(ONEVS100_BUILD_VERSION "unknown")
if(GIT_FOUND)
  execute_process(
    COMMAND "${GIT_EXECUTABLE}" -C "${CMAKE_CURRENT_SOURCE_DIR}" log -1 --format=%cs-%h
    OUTPUT_VARIABLE git_version OUTPUT_STRIP_TRAILING_WHITESPACE ERROR_QUIET RESULT_VARIABLE git_result)
  if(git_result EQUAL 0 AND git_version)
    set(ONEVS100_BUILD_VERSION "${git_version}")
  endif()
endif()

# Embed the UI font as a byte array.
set(FONT_FILE "${imgui_SOURCE_DIR}/misc/fonts/Roboto-Medium.ttf")
set(FONT_HEADER "${CMAKE_CURRENT_BINARY_DIR}/generated/font_data.h")
file(READ "${FONT_FILE}" font_hex HEX)
string(REGEX REPLACE "([0-9a-f][0-9a-f])" "0x\\1," font_bytes "${font_hex}")
file(WRITE "${FONT_HEADER}" "#pragma once\ninline constexpr unsigned char kFontData[] = {${font_bytes}};\n")

add_executable(onevs100-launcher WIN32
  main.cpp
  files.cpp
  update_flow.cpp
  updater.cpp
  ${imgui_SOURCE_DIR}/imgui.cpp
  ${imgui_SOURCE_DIR}/imgui_draw.cpp
  ${imgui_SOURCE_DIR}/imgui_tables.cpp
  ${imgui_SOURCE_DIR}/imgui_widgets.cpp
  ${imgui_SOURCE_DIR}/misc/cpp/imgui_stdlib.cpp
  ${imgui_SOURCE_DIR}/backends/imgui_impl_sdl3.cpp
  ${imgui_SOURCE_DIR}/backends/imgui_impl_sdlrenderer3.cpp)
target_include_directories(onevs100-launcher PRIVATE
  ${imgui_SOURCE_DIR} ${imgui_SOURCE_DIR}/backends ${CMAKE_CURRENT_BINARY_DIR}/generated
  ${CMAKE_CURRENT_SOURCE_DIR}/../native/src)
target_compile_definitions(onevs100-launcher PRIVATE IMGUI_DISABLE_DEMO_WINDOWS)
# Only main.cpp shows the version, so only it rebuilds when the version changes.
set_source_files_properties(main.cpp PROPERTIES
  COMPILE_DEFINITIONS "ONEVS100_BUILD_VERSION=\"${ONEVS100_BUILD_VERSION}\";ONEVS100_RELEASE_VERSION=\"${ONEVS100_RELEASE_VERSION}\"")
# toml++ errors are caught by value; MSVC/clang-cl needs unwind semantics
# or a corrupt onevs100.toml would terminate instead of showing the status.
if (MSVC)
  target_compile_options(onevs100-launcher PRIVATE /EHsc)
endif()
target_link_libraries(onevs100-launcher PRIVATE SDL3::SDL3-static tomlplusplus::tomlplusplus onevs100_avatar velopack)
# System libraries the Rust standard library and Velopack call into.
if(WIN32)
  target_link_libraries(onevs100-launcher PRIVATE
    advapi32 bcrypt kernel32 ntdll ole32 oleaut32 shell32 user32 ws2_32)
elseif(NOT APPLE)
  target_link_libraries(onevs100-launcher PRIVATE dl m pthread)
endif()
set_target_properties(onevs100-launcher PROPERTIES OUTPUT_NAME launcher)
