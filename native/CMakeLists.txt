# onevs100 - ReXGlue Recompiled Project
#
# SDK boilerplate lives in generated/rexglue.cmake.

cmake_minimum_required(VERSION 3.25)
project(onevs100 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

include(generated/rexglue-frozen.cmake)

# Sources
set(ONEVS100_SOURCES
    src/main.cpp
    src/offline_network.cpp
    src/offline_profile.cpp
    src/local_profile.cpp
    src/dialogue_audio.cpp
    src/dialogue_hook.cpp
    src/native_room.cpp
    src/native_room_hook.cpp
    src/native_controls.cpp
    src/native_video.cpp
    src/avatar_probe.cpp
    src/avatar_native/native_provider.cpp
    src/avatar_native/local_avatar.cpp
    src/avatar_native/native_assets.cc
    src/avatar_native/xna_avatar_format.cc
    src/avatar_native/third_party/mspack/lzxd.c
    src/avatar_native/third_party/mspack/system.c
    src/avatar_native/third_party/mspack/logging.cc
    src/avatar_texture_upload.cpp
    src/avatar_model_probe.cpp
    src/avatar_joint_derive.cpp
    src/avatar_joint_probe.cpp
    src/exit_probe.cpp
    src/crash_capture.cpp
    src/shared_body.cpp
    src/platform_compat.cpp
    src/round_even.cpp
    src/jpeg_probe.cpp
    src/jump_hooks.cpp
)

if(WIN32)
    add_executable(onevs100 WIN32 ${ONEVS100_SOURCES})
    target_link_libraries(onevs100 PRIVATE dbghelp comdlg32 winmm hid setupapi mfplay ole32)
else()
    add_executable(onevs100 ${ONEVS100_SOURCES})
endif()

rexglue_setup_target(onevs100 GPU_PLUGINS xenos)
if(WIN32)
    target_link_options(onevs100 PRIVATE /MAP:onevs100.map)
endif()

target_include_directories(onevs100 PRIVATE src/avatar_native)
# Steamworks SDK (not included): pass -DONEVS100_STEAM_SDK=<path to the sdk folder>.
set(ONEVS100_STEAM_SDK "$ENV{STEAMWORKS_SDK}" CACHE PATH "Steamworks SDK folder (contains public/ and redistributable_bin/)")
if(NOT EXISTS "${ONEVS100_STEAM_SDK}/public/steam/steam_api.h")
  message(FATAL_ERROR "Set ONEVS100_STEAM_SDK (or the STEAMWORKS_SDK environment variable) to your Steamworks SDK 'sdk' folder.")
endif()
target_include_directories(onevs100 PRIVATE "${ONEVS100_STEAM_SDK}/public")
target_link_libraries(onevs100 PRIVATE "${ONEVS100_STEAM_SDK}/redistributable_bin/win64/steam_api64.lib")
add_custom_command(TARGET onevs100 POST_BUILD COMMAND ${CMAKE_COMMAND} -E copy_if_different
  "${ONEVS100_STEAM_SDK}/redistributable_bin/win64/steam_api64.dll" "$<TARGET_FILE_DIR:onevs100>")
target_include_directories(onevs100 PRIVATE src/avatar_native/third_party/mspack)

add_executable(avatar_catalog_test
 src/avatar_native/catalog_test.cpp src/avatar_native/native_assets.cc
 src/avatar_native/xna_avatar_format.cc src/avatar_native/third_party/mspack/lzxd.c
 src/avatar_native/third_party/mspack/system.c src/avatar_native/third_party/mspack/logging.cc)
target_include_directories(avatar_catalog_test PRIVATE src/avatar_native src/avatar_native/third_party/mspack)
target_compile_definitions(avatar_catalog_test PRIVATE NATIVE_AVATAR_CATALOG_TEST NOMINMAX)
target_link_libraries(avatar_catalog_test PRIVATE rex::runtime)
