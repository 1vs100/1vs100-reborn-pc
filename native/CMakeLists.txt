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
    src/avatar_probe.cpp
    src/avatar_native/native_provider.cpp
    src/avatar_native/local_avatar.cpp
    src/avatar_native/native_assets.cc
    src/avatar_native/xna_avatar_format.cc
    src/avatar_native/third_party/mspack/lzxd.c
    src/avatar_native/third_party/mspack/system.c
    src/avatar_native/third_party/mspack/logging.cc
    src/exit_probe.cpp
    src/shared_body.cpp
    src/platform_compat.cpp
    src/round_even.cpp
    src/jpeg_probe.cpp
    src/jump_hooks.cpp
)

if(WINDOWS_STORE)
    # UWP has no minidumps, PlaySound, Win32 dialogs, raw HID or mfplay.
    # The portable stubs stand in until UWP versions exist.
    list(APPEND ONEVS100_SOURCES
        src/dialogue_audio_posix.cpp
        src/local_profile_dialog_posix.cpp
        src/native_controls_posix.cpp
        src/native_video_posix.cpp
    )
elseif(WIN32)
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

if(WINDOWS_STORE)
    add_executable(onevs100 WIN32 ${ONEVS100_SOURCES})
    target_link_libraries(onevs100 PRIVATE ws2_32)
elseif(WIN32)
    add_executable(onevs100 WIN32 ${ONEVS100_SOURCES})
    target_link_libraries(onevs100 PRIVATE dbghelp comdlg32 winmm hid setupapi mfplay ole32 ws2_32)
else()
    add_executable(onevs100 ${ONEVS100_SOURCES})
endif()

rexglue_setup_target(onevs100 GPU_PLUGINS xenos)
if(WIN32)
    target_link_options(onevs100 PRIVATE /MAP:onevs100.map)
    target_compile_definitions(onevs100 PRIVATE ONEVS100_RUNTIME_DLL="$<TARGET_FILE_NAME:rex::runtime>")
else()
    # The executable's RUNPATH is $ORIGIN.
    add_custom_command(TARGET onevs100 POST_BUILD COMMAND ${CMAKE_COMMAND} -E copy_if_different
      "$<TARGET_FILE:rex::runtime>" "$<TARGET_FILE_DIR:onevs100>")
endif()

target_include_directories(onevs100 PRIVATE src/avatar_native third_party/1vs100-mm-protocol)
target_include_directories(onevs100 PRIVATE src/avatar_native/third_party/mspack)
