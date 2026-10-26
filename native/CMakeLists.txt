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
    src/online_service.cpp
    src/gameserver_redirect.cpp
    src/gameserver_account.cpp
    src/gameserver_launch_data.cpp
    src/gameserver_xhttp.cpp
    src/lua_print_log.cpp
    src/content_install.cpp
    src/runtime_export.cpp
    src/gameserver_lsp.cpp
    src/gameserver_qos.cpp
    src/gameserver_massive_cache.cpp
    src/gameserver_session.cpp
    src/gameserver_qnet.cpp
    src/gameserver_presence.cpp
    src/guest_call.cpp
    src/matchmaking_client.cpp
    third_party/1vs100-mm-protocol/Protocol.cpp
    src/guest_sockets.cpp
    src/guest_printf.cpp
    src/offline_profile.cpp
    src/local_profile.cpp
    src/dialogue_hook.cpp
    src/native_room_hook.cpp
    src/room_clock.cpp
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
    src/shared_body.cpp
    src/platform_compat.cpp
    src/round_even.cpp
    src/jpeg_probe.cpp
    src/jump_hooks.cpp
)

if(WIN32)
    list(APPEND ONEVS100_SOURCES
        src/crash_capture.cpp
        src/dialogue_audio_win32.cpp
        src/local_profile_dialog_win32.cpp
        src/native_controls_win32.cpp
        src/native_video_win32.cpp
    )
else()
    list(APPEND ONEVS100_SOURCES
        src/dialogue_audio_posix.cpp
        src/local_profile_dialog_posix.cpp
        src/native_controls_posix.cpp
        src/native_video_posix.cpp
        src/video_driver_posix.cpp
    )
endif()

if(WIN32)
    add_executable(onevs100 WIN32 ${ONEVS100_SOURCES})
    target_link_libraries(onevs100 PRIVATE dbghelp comdlg32 winmm hid setupapi mfplay ole32)
else()
    add_executable(onevs100 ${ONEVS100_SOURCES})
endif()

rexglue_setup_target(onevs100 GPU_PLUGINS xenos)
if(WIN32)
    target_link_options(onevs100 PRIVATE /MAP:onevs100.map)
else()
    # The executable's RUNPATH is $ORIGIN.
    add_custom_command(TARGET onevs100 POST_BUILD COMMAND ${CMAKE_COMMAND} -E copy_if_different
      "$<TARGET_FILE:rex::runtime>" "$<TARGET_FILE_DIR:onevs100>")
endif()

target_include_directories(onevs100 PRIVATE src/avatar_native third_party/1vs100-mm-protocol)
# Steamworks SDK (not included). Without it the room transport reports Steam as unavailable.
set(ONEVS100_STEAM_SDK "$ENV{STEAMWORKS_SDK}" CACHE PATH "Steamworks SDK folder (contains public/ and redistributable_bin/)")
if(EXISTS "${ONEVS100_STEAM_SDK}/public/steam/steam_api.h")
  if(WIN32)
    set(ONEVS100_STEAM_LIB "${ONEVS100_STEAM_SDK}/redistributable_bin/win64/steam_api64.lib")
    set(ONEVS100_STEAM_RUNTIME "${ONEVS100_STEAM_SDK}/redistributable_bin/win64/steam_api64.dll")
  else()
    set(ONEVS100_STEAM_LIB "${ONEVS100_STEAM_SDK}/redistributable_bin/linux64/libsteam_api.so")
    set(ONEVS100_STEAM_RUNTIME "${ONEVS100_STEAM_LIB}")
  endif()
  target_sources(onevs100 PRIVATE src/native_room_steam.cpp)
  target_include_directories(onevs100 PRIVATE "${ONEVS100_STEAM_SDK}/public")
  target_link_libraries(onevs100 PRIVATE "${ONEVS100_STEAM_LIB}")
  add_custom_command(TARGET onevs100 POST_BUILD COMMAND ${CMAKE_COMMAND} -E copy_if_different
    "${ONEVS100_STEAM_RUNTIME}" "$<TARGET_FILE_DIR:onevs100>")
  message(STATUS "Steamworks SDK: ${ONEVS100_STEAM_SDK}")
else()
  target_sources(onevs100 PRIVATE src/native_room_unavailable.cpp)
  if(ONEVS100_STEAM_SDK)
    message(WARNING "No public/steam/steam_api.h in ONEVS100_STEAM_SDK=${ONEVS100_STEAM_SDK}: building without Steam parties")
  else()
    message(STATUS "Steamworks SDK not set: building without Steam parties")
  endif()
endif()
target_include_directories(onevs100 PRIVATE src/avatar_native/third_party/mspack)

add_executable(avatar_catalog_test
 src/avatar_native/catalog_test.cpp src/avatar_native/native_assets.cc
 src/avatar_native/xna_avatar_format.cc src/avatar_native/third_party/mspack/lzxd.c
 src/avatar_native/third_party/mspack/system.c src/avatar_native/third_party/mspack/logging.cc)
target_include_directories(avatar_catalog_test PRIVATE src/avatar_native src/avatar_native/third_party/mspack)
target_compile_definitions(avatar_catalog_test PRIVATE NATIVE_AVATAR_CATALOG_TEST NOMINMAX)
target_link_libraries(avatar_catalog_test PRIVATE rex::runtime)
