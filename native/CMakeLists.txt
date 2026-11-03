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
    src/io_probe.cpp
    src/gpu_probe.cpp
    src/xma_kick.cpp
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
    # First-run game-file onboarding (browser upload / USB pickers with a
    # hash manifest), adapted from tools/uwp onboarding sources.
    set(UWP_ONBOARDING_SOURCES
        src/uwp_onboarding/gate.cpp
        src/uwp_onboarding/app.cpp
        src/uwp_onboarding/manifest/manifest.cpp
        src/uwp_onboarding/net/http_server.cpp
        src/uwp_onboarding/net/lan_address.cpp
        src/uwp_onboarding/storage/catalog.cpp
        src/uwp_onboarding/storage/file_io.cpp
        src/uwp_onboarding/storage/file_store.cpp
        src/uwp_onboarding/storage/importer.cpp
        src/uwp_onboarding/storage/sha256.cpp
        src/uwp_onboarding/ui/draw.cpp
        src/uwp_onboarding/ui/focus.cpp
        src/uwp_onboarding/thirdparty/qrcodegen/qrcodegen.cpp
    )
    list(APPEND ONEVS100_SOURCES ${UWP_ONBOARDING_SOURCES})
    # C++/WinRT coroutines need unwind semantics, like the launcher.
    set_source_files_properties(${UWP_ONBOARDING_SOURCES} PROPERTIES
        COMPILE_OPTIONS "/EHsc"
        COMPILE_DEFINITIONS "NOMINMAX;WIN32_LEAN_AND_MEAN;UNICODE;_UNICODE")
    set(UWP_ONBOARDING_GENERATED "${CMAKE_CURRENT_BINARY_DIR}/generated-uwp")
    file(MAKE_DIRECTORY "${UWP_ONBOARDING_GENERATED}")
    add_custom_command(
        OUTPUT "${UWP_ONBOARDING_GENERATED}/embedded_manifest.h"
        COMMAND ${CMAKE_COMMAND} -DINPUT=${CMAKE_SOURCE_DIR}/../package/manifest.txt
                -DOUTPUT=${UWP_ONBOARDING_GENERATED}/embedded_manifest.h -DNAME=kEmbeddedManifest -DTYPE=char
                -DTERMINATE=ON -P ${CMAKE_SOURCE_DIR}/../tools/uwp/embed.cmake
        DEPENDS "${CMAKE_SOURCE_DIR}/../package/manifest.txt" "${CMAKE_SOURCE_DIR}/../tools/uwp/embed.cmake"
        COMMENT "Embedding UWP onboarding manifest")
    add_custom_command(
        OUTPUT "${UWP_ONBOARDING_GENERATED}/web_page.h"
        COMMAND ${CMAKE_COMMAND} -DINPUT=${CMAKE_SOURCE_DIR}/src/uwp_onboarding/web/index.html
                -DOUTPUT=${UWP_ONBOARDING_GENERATED}/web_page.h -DNAME=kWebPage -DTYPE=unsigned\ char
                -P ${CMAKE_SOURCE_DIR}/../tools/uwp/embed.cmake
        DEPENDS "${CMAKE_SOURCE_DIR}/src/uwp_onboarding/web/index.html"
                "${CMAKE_SOURCE_DIR}/../tools/uwp/embed.cmake"
        COMMENT "Embedding UWP onboarding web page")
    # The generated headers are compiled in through a stub TU.
    list(APPEND ONEVS100_SOURCES
        "${UWP_ONBOARDING_GENERATED}/embedded_manifest.h"
        "${UWP_ONBOARDING_GENERATED}/web_page.h")
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
    target_link_libraries(onevs100 PRIVATE ws2_32 bcrypt)
    target_include_directories(onevs100 PRIVATE
        src/uwp_onboarding src/uwp_onboarding/thirdparty/qrcodegen
        "${CMAKE_CURRENT_BINARY_DIR}/generated-uwp")
    if(DEFINED REXGLUE_SDL3_UWP_PREFIX)
        target_include_directories(onevs100 PRIVATE "${REXGLUE_SDL3_UWP_PREFIX}/include")
    endif()
elseif(WIN32)
    add_executable(onevs100 WIN32 ${ONEVS100_SOURCES})
    target_link_libraries(onevs100 PRIVATE dbghelp comdlg32 winmm hid setupapi mfplay ole32 ws2_32)
else()
    add_executable(onevs100 ${ONEVS100_SOURCES})
endif()
#
# Workaround: the SDK's macOS Vulkan staging uses ${REXGLUE_ROOT} to locate
# MoltenVK_icd.json, but REXGLUE_ROOT is a plain set() in the SDK's own
# directory scope, so it is empty when rexglue_setup_target() runs here.
# Define it in our scope until the SDK fork propagates it to consumers.
if(APPLE AND DEFINED REXSDK_DIR AND NOT DEFINED REXGLUE_ROOT)
  set(REXGLUE_ROOT "${REXSDK_DIR}")
endif()

rexglue_setup_target(onevs100 GPU_PLUGINS xenos)

# The title's own D3D12 renderer. It replaces the xenos plugin when the
# native_renderer setting is on; see native/src/native_gpu/. UWP is the target
# that can be tested on the console.
if(WIN32)
    set(PLUME_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
    add_subdirectory(third_party/plume plume)
    if(WINDOWS_STORE)
        # AppContainer has no HWND swap chain; plume takes the CoreWindow path.
        target_compile_definitions(plume PUBLIC PLUME_UWP=1)
    endif()
    target_sources(onevs100 PRIVATE src/native_gpu/gpu_native.cpp)
    target_include_directories(onevs100 PRIVATE third_party/glm)
    target_link_libraries(onevs100 PRIVATE plume)
endif()

if(WIN32)
    target_sources(onevs100 PRIVATE src/audio_xaudio2.cpp)
    target_link_libraries(onevs100 PRIVATE xaudio2)
    target_link_options(onevs100 PRIVATE /MAP:onevs100.map)
    target_compile_definitions(onevs100 PRIVATE ONEVS100_RUNTIME_DLL="$<TARGET_FILE_NAME:rex::runtime>")
else()
    # The executable's RUNPATH is $ORIGIN.
    add_custom_command(TARGET onevs100 POST_BUILD COMMAND ${CMAKE_COMMAND} -E copy_if_different
      "$<TARGET_FILE:rex::runtime>" "$<TARGET_FILE_DIR:onevs100>")
endif()

target_include_directories(onevs100 PRIVATE src/avatar_native third_party/1vs100-mm-protocol)
target_include_directories(onevs100 PRIVATE src/avatar_native/third_party/mspack)
