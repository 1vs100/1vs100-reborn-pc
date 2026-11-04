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
    src/platform_compat.cpp
    src/round_even.cpp
    src/xma_kick.cpp
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
        src/death_marker.cpp
        "${UWP_ONBOARDING_GENERATED}/embedded_manifest.h"
        "${UWP_ONBOARDING_GENERATED}/web_page.h")
elseif(WIN32)
    list(APPEND ONEVS100_SOURCES
        src/crash_capture.cpp
        src/death_marker.cpp
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
# that can be tested on the console, so it compiles there too.
set(PLUME_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
if(WIN32)
    add_subdirectory(third_party/plume plume)
    target_link_libraries(onevs100 PRIVATE plume)
    if(WINDOWS_STORE)
        # AppContainer has no HWND swap chain; plume takes the CoreWindow path.
        target_compile_definitions(plume PUBLIC PLUME_UWP=1)
    endif()
endif()
if(WIN32)
    target_sources(onevs100 PRIVATE
        src/render/display_settings.cpp
        src/render/settings.cpp
        src/render/edge.cpp
        src/render/edge_draw.cpp
        src/render/edge_memory.cpp
        src/render/edge_watch.cpp
        src/render/gpu_capture.cpp
        src/render/gpu_cmdbuf.cpp
        src/render/gpu_draw.cpp
        src/render/gpu_formats.cpp
        src/render/gpu_memory.cpp
        src/render/gpu_overlay.cpp
        src/render/gpu_pointer.cpp
        src/render/gpu_renderer.cpp
        src/render/gpu_shader_pack.cpp
        src/render/gpu_texture_decode.cpp
        src/render/gpu_tracker.cpp
    )
    target_include_directories(onevs100 PRIVATE src)
    # A development build may read the shader pack from the source tree.
    target_compile_definitions(onevs100 PRIVATE
        "ONEVS100_SOURCE_DIR=\"${CMAKE_SOURCE_DIR}\"")
    target_include_directories(onevs100 PRIVATE third_party/glm)
    # The app shell only offers native_renderer when the renderer is compiled in.
    target_compile_definitions(onevs100 PRIVATE ONEVS100_NATIVE_GPU=1)
    if(WINDOWS_STORE)
        # A package that statically imports d3d12.dll is refused at activation;
        # the SDK load-libraries it on UWP for the same reason.
        target_link_options(onevs100 PRIVATE "LINKER:/DELAYLOAD:d3d12.dll")
        target_link_libraries(onevs100 PRIVATE delayimp)
    endif()

    # Xenos shader microcode to DXIL, plus the X4 pack reader. The core library
    # compiles the translator in; the command-line tool stays off.
    enable_language(C)
    set(XENOS_RECOMP_THIRDPARTY_ROOT
        "${CMAKE_CURRENT_SOURCE_DIR}/third_party/xenosrecomp/thirdparty")
    set(XENOS_RECOMP_BUILD_CLI OFF CACHE BOOL "" FORCE)
    set(XENOS_RECOMP_DXIL ON CACHE BOOL "" FORCE)
    set(ZSTD_BUILD_PROGRAMS OFF CACHE BOOL "" FORCE)
    set(ZSTD_BUILD_SHARED OFF CACHE BOOL "" FORCE)
    set(ZSTD_BUILD_TESTS OFF CACHE BOOL "" FORCE)
    add_subdirectory("${XENOS_RECOMP_THIRDPARTY_ROOT}" xenosrecomp_thirdparty EXCLUDE_FROM_ALL)
    add_subdirectory("${CMAKE_CURRENT_SOURCE_DIR}/third_party/xenosrecomp/XenosRecomp" xenosrecomp
        EXCLUDE_FROM_ALL)
    # Precompiled headers are skipped for this cross toolchain, so the vendored
    # translation units get the shared header force-included instead.
    target_compile_options(xenosrecomp_core PRIVATE
        "/FI${CMAKE_CURRENT_SOURCE_DIR}/third_party/xenosrecomp/XenosRecomp/pch.h")
    target_link_libraries(onevs100 PRIVATE xenosrecomp_core xxHash::xxhash)
    if(WINDOWS_STORE)
        # Translation runs at run time, so the compiler ships beside the exe.
        target_link_options(onevs100 PRIVATE "LINKER:/DELAYLOAD:dxcompiler.dll"
            "LINKER:/DELAYLOAD:dxil.dll")
        target_link_libraries(onevs100 PRIVATE delayimp)
        add_custom_command(TARGET onevs100 POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E copy_if_different
                "${XENOS_RECOMP_THIRDPARTY_ROOT}/dxc-bin/bin/x64/dxcompiler.dll"
                "${XENOS_RECOMP_THIRDPARTY_ROOT}/dxc-bin/bin/x64/dxil.dll"
                "$<TARGET_FILE_DIR:onevs100>")
    endif()
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
