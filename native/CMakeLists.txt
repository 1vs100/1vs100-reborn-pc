# onevs100 - ReXGlue Recompiled Project
#
# SDK boilerplate lives in generated/rexglue.cmake.

cmake_minimum_required(VERSION 3.25)
project(onevs100 LANGUAGES C CXX)

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
    src/guest_memory_ops.cpp
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
    src/avatar_view/avatar_view.cpp
    src/avatar_view/avatar_dialog.cpp
    src/exit_probe.cpp
    src/fatal_dialog.cpp
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
    # First-run game-file onboarding: phone upload or USB pickers against a
    # hash manifest. The view is its own repository, kept in third_party and
    # built as its own target.
    set(ONBOARDING_UI_FLAVOR "sdl3" CACHE STRING "Onboarding view toolkit: sdl3 or imgui")
    set(ONBOARDING_UI_MANIFEST "${CMAKE_SOURCE_DIR}/../package/manifest.txt" CACHE FILEPATH
        "Game files the onboarding view checks for")
    add_subdirectory(third_party/deployment-onboarding-ui
                     "${CMAKE_CURRENT_BINARY_DIR}/deployment-onboarding-ui")
    if(DEFINED REXGLUE_SDL3_UWP_PREFIX)
        target_include_directories(deployment_onboarding_ui PRIVATE
            "${REXGLUE_SDL3_UWP_PREFIX}/include")
    endif()
    # Only the view's own frame loop needs the console renderer hint.
    target_compile_definitions(deployment_onboarding_ui PRIVATE REX_PLATFORM_WINRT=1)
    # The transports the view cannot own: sockets, pickers, USB presence.
    set(ONBOARDING_HOST_SOURCES
        src/onboarding_host/http_server.cpp
        src/onboarding_host/importer.cpp
        src/onboarding_host/lan_address.cpp
        src/onboarding_host/platform.cpp)
    # C++/WinRT coroutines need unwind semantics, like the launcher.
    set_source_files_properties(${ONBOARDING_HOST_SOURCES} PROPERTIES
        COMPILE_OPTIONS "/EHsc"
        COMPILE_DEFINITIONS "NOMINMAX;WIN32_LEAN_AND_MEAN;UNICODE;_UNICODE")
    list(APPEND ONEVS100_SOURCES
        ${ONBOARDING_HOST_SOURCES}
        src/avatar_view/avatar_server_winrt.cpp
        src/death_marker.cpp)
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
        src/fatal_signal_posix.cpp
        src/local_profile_dialog_posix.cpp
        src/native_controls_posix.cpp
        src/native_video_posix.cpp
        src/video_driver_posix.cpp
    )
endif()

if(WINDOWS_STORE)
    add_executable(onevs100 WIN32 ${ONEVS100_SOURCES})
    target_link_libraries(onevs100 PRIVATE ws2_32 bcrypt onboarding_ui::onboarding_ui)
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
if(WINDOWS_STORE)
    # Both consoles' CPUs (Jaguar, Zen 2) have AVX: VEX encodings save the register copies the recompiled vector
    # code is full of.
    target_compile_options(onevs100 PRIVATE /clang:-mavx)
    target_compile_options(onevs100_recomp PRIVATE /clang:-mavx)
endif()

# The title's own renderer, replacing the xenos plugin when the native_renderer
# setting is on: D3D12 on Windows and UWP, Vulkan elsewhere.
set(PLUME_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
# SDL creates the Vulkan surface, for Wayland and X11 alike.
if(NOT WIN32)
    set(PLUME_SDL_VULKAN_ENABLED ON CACHE BOOL "" FORCE)
endif()
add_subdirectory(third_party/plume plume)
target_link_libraries(onevs100 PRIVATE plume)
if(WINDOWS_STORE)
    # AppContainer has no HWND swap chain; plume takes the CoreWindow path.
    target_compile_definitions(plume PUBLIC PLUME_UWP=1)
endif()
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
target_include_directories(onevs100 PRIVATE third_party/glm "${REXSDK_DIR}/thirdparty/renderdoc")
# Microsoft publishes DXC for Windows and Linux x86_64 only. Elsewhere the
# native renderer is off by default: the game links a DxcCreateInstance stub
# that fails translation cleanly, and runs from prebuilt shader packs.
if(WIN32 OR (CMAKE_SYSTEM_NAME STREQUAL "Linux" AND CMAKE_SYSTEM_PROCESSOR MATCHES "^(x86_64|AMD64)$"))
    set(_native_gpu_default ON)
else()
    set(_native_gpu_default OFF)
endif()
option(ONEVS100_NATIVE_GPU "Build the native renderer (needs a DXC release for this platform)" ${_native_gpu_default})
# The app shell only offers native_renderer when the renderer is compiled in.
if(ONEVS100_NATIVE_GPU)
    target_compile_definitions(onevs100 PRIVATE ONEVS100_NATIVE_GPU=1)
else()
    target_compile_definitions(onevs100 PRIVATE ONEVS100_NATIVE_GPU=0)
endif()
if(WINDOWS_STORE)
    # A package that statically imports d3d12.dll is refused at activation;
    # the SDK load-libraries it on UWP for the same reason.
    target_link_options(onevs100 PRIVATE "LINKER:/DELAYLOAD:d3d12.dll")
    target_link_libraries(onevs100 PRIVATE delayimp)
endif()

# Xenos shader microcode to DXIL and SPIR-V, plus the X4 pack reader. The core
# library compiles the translator in; the command-line tool stays off.
enable_language(C)
set(XENOS_RECOMP_THIRDPARTY_ROOT
    "${CMAKE_CURRENT_SOURCE_DIR}/third_party/xenosrecomp/thirdparty")
set(ONEVS100_DXC_TAG "v1.8.2407")
set(ONEVS100_DXC_LINUX_URL "https://github.com/microsoft/DirectXShaderCompiler/releases/download/${ONEVS100_DXC_TAG}/linux_dxc_2024_07_31.x86_64.tar.gz")
set(ONEVS100_DXC_LINUX_SHA256 "b58725ac191df0686994fb9d54d27ee8dd3f180b023d562733112a499ea1195c")
set(ONEVS100_DXC_WIN_URL "https://github.com/microsoft/DirectXShaderCompiler/releases/download/${ONEVS100_DXC_TAG}/dxc_2024_07_31.zip")
set(ONEVS100_DXC_WIN_SHA256 "e2627f004f0f9424d8c71ea1314d04f38c5a5096884ae9217f1f18bd320267b5")

# Fetches a pinned DXC archive at configure time and copies files from it.
# The URL and hash pin the bytes. The archive is staged under the build dir.
# Remaining arguments are <extract-relative source, absolute destination>
# pairs. Skips download and extract when the stamp matches and every
# destination exists. If the download fails but every destination exists,
# warns and keeps them, so an offline re-configure still works. Destinations
# must be git-ignored.
function(onevs100_fetch_dxc _url _sha256 _archive_name)
    set(_archive "${CMAKE_CURRENT_BINARY_DIR}/${_archive_name}")
    set(_stamp "${_archive}.stamp")
    set(_extract "${_archive}.extract")
    set(_srcs "")
    set(_dsts "")
    set(_take_src TRUE)
    foreach(_a IN LISTS ARGN)
        if(_take_src)
            list(APPEND _srcs "${_a}")
            set(_take_src FALSE)
        else()
            list(APPEND _dsts "${_a}")
            set(_take_src TRUE)
        endif()
    endforeach()
    list(LENGTH _srcs _n_srcs)
    list(LENGTH _dsts _n_dsts)
    if(NOT _n_srcs EQUAL _n_dsts)
        message(FATAL_ERROR "onevs100_fetch_dxc needs even <src dst> pairs")
    endif()
    set(_skip TRUE)
    if(EXISTS "${_stamp}")
        file(READ "${_stamp}" _have)
        string(STRIP "${_have}" _have)
        if(_have STREQUAL "${_url} ${_sha256}")
            foreach(_src _dst IN ZIP_LISTS _srcs _dsts)
                if(NOT EXISTS "${_dst}")
                    set(_skip FALSE)
                endif()
            endforeach()
        else()
            set(_skip FALSE)
        endif()
    else()
        set(_skip FALSE)
    endif()
    if(_skip)
        return()
    endif()
    set(_have_all TRUE)
    foreach(_src _dst IN ZIP_LISTS _srcs _dsts)
        if(NOT EXISTS "${_dst}")
            set(_have_all FALSE)
        endif()
    endforeach()
    file(DOWNLOAD "${_url}" "${_archive}" STATUS _status)
    list(GET _status 0 _code)
    if(NOT _code EQUAL 0)
        list(GET _status 1 _detail)
        if(_have_all)
            message(WARNING "onevs100_fetch_dxc: download failed (${_detail}); keeping existing files")
            return()
        endif()
        message(FATAL_ERROR "onevs100_fetch_dxc: download failed (${_detail}) and destinations are missing")
    endif()
    file(SHA256 "${_archive}" _actual)
    string(TOLOWER "${_actual}" _actual)
    string(TOLOWER "${_sha256}" _want)
    if(NOT _actual STREQUAL _want)
        file(REMOVE "${_archive}")
        if(_have_all)
            message(WARNING "onevs100_fetch_dxc: hash mismatch for ${_url}; keeping existing files")
            return()
        endif()
        message(FATAL_ERROR "onevs100_fetch_dxc: hash mismatch for ${_url}")
    endif()
    file(REMOVE_RECURSE "${_extract}")
    file(ARCHIVE_EXTRACT INPUT "${_archive}" DESTINATION "${_extract}")
    foreach(_src _dst IN ZIP_LISTS _srcs _dsts)
        get_filename_component(_dst_dir "${_dst}" DIRECTORY)
        file(MAKE_DIRECTORY "${_dst_dir}")
        file(COPY_FILE "${_extract}/${_src}" "${_dst}")
    endforeach()
    file(WRITE "${_stamp}" "${_url} ${_sha256}")
endfunction()

# The desktop exe carries no static dxcompiler/dxil import (checked with
# llvm-readobj --coff-imports); the shader translator loads them with
# LoadLibraryW and falls back without them. The .lib below is a build input
# only, so the zip and installer ship no DXC DLLs.
if(ONEVS100_NATIVE_GPU)
  if(WIN32)
    onevs100_fetch_dxc("${ONEVS100_DXC_WIN_URL}" "${ONEVS100_DXC_WIN_SHA256}"
        "windows_dxc_2024_07_31.zip"
        "bin/x64/dxcompiler.dll" "${XENOS_RECOMP_THIRDPARTY_ROOT}/dxc-bin/bin/x64/dxcompiler.dll"
        "bin/x64/dxil.dll" "${XENOS_RECOMP_THIRDPARTY_ROOT}/dxc-bin/bin/x64/dxil.dll"
        "bin/x64/dxc.exe" "${XENOS_RECOMP_THIRDPARTY_ROOT}/dxc-bin/bin/x64/dxc.exe"
        "lib/x64/dxcompiler.lib" "${XENOS_RECOMP_THIRDPARTY_ROOT}/dxc-bin/lib/x64/dxcompiler.lib")
  elseif(CMAKE_SYSTEM_NAME STREQUAL "Linux" AND CMAKE_SYSTEM_PROCESSOR MATCHES "^(x86_64|AMD64)$")
    onevs100_fetch_dxc("${ONEVS100_DXC_LINUX_URL}" "${ONEVS100_DXC_LINUX_SHA256}"
        "linux_dxc_2024_07_31.x86_64.tar.gz"
        "lib/libdxcompiler.so" "${XENOS_RECOMP_THIRDPARTY_ROOT}/dxc-bin/lib/x64/libdxcompiler.so"
        "lib/libdxil.so" "${XENOS_RECOMP_THIRDPARTY_ROOT}/dxc-bin/lib/x64/libdxil.so"
        "bin/dxc" "${XENOS_RECOMP_THIRDPARTY_ROOT}/dxc-bin/bin/x64/dxc-linux")
  else()
    # Microsoft publishes DXC for Windows and Linux x86_64 only. Without it
    # the renderer links the stub translator and runs from prebuilt shader
    # packs; runtime translation is unavailable.
    message(WARNING "The native renderer needs DXC; no release exists for "
      "${CMAKE_SYSTEM_NAME} ${CMAKE_SYSTEM_PROCESSOR} "
      "(linking the stub translator instead)")
    set(_native_gpu_without_dxc TRUE)
  endif()
endif()
if(NOT ONEVS100_NATIVE_GPU OR _native_gpu_without_dxc)
  add_library(onevs100_stub_dxc STATIC src/stub_dxc.c)
  add_library(Microsoft::DirectXShaderCompiler ALIAS onevs100_stub_dxc)
  message(STATUS "Native renderer without DXC: runtime shader translation unavailable, shader packs required")
endif()
set(XENOS_RECOMP_BUILD_CLI OFF CACHE BOOL "" FORCE)
if(ONEVS100_NATIVE_GPU)
  set(XENOS_RECOMP_DXIL ${WIN32} CACHE BOOL "" FORCE)
else()
  set(XENOS_RECOMP_DXIL OFF CACHE BOOL "" FORCE)
endif()
set(ZSTD_BUILD_PROGRAMS OFF CACHE BOOL "" FORCE)
set(ZSTD_BUILD_SHARED OFF CACHE BOOL "" FORCE)
set(ZSTD_BUILD_TESTS OFF CACHE BOOL "" FORCE)
add_subdirectory("${XENOS_RECOMP_THIRDPARTY_ROOT}" xenosrecomp_thirdparty EXCLUDE_FROM_ALL)
add_subdirectory("${CMAKE_CURRENT_SOURCE_DIR}/third_party/xenosrecomp/XenosRecomp" xenosrecomp
    EXCLUDE_FROM_ALL)
if(NOT ONEVS100_NATIVE_GPU OR _native_gpu_without_dxc)
  target_include_directories(xenosrecomp_core PRIVATE "${XENOS_RECOMP_THIRDPARTY_ROOT}/dxc-bin/inc")
endif()
if(APPLE)
  # libc++ keeps the parallel-algorithm dispatcher in libc++experimental.a, and
  # the pack builder uses std::execution::par. The driver adds that archive only
  # when the link step itself carries -fexperimental-library.
  target_link_libraries(xenosrecomp_core PUBLIC c++experimental)
endif()
# Precompiled headers are skipped for the cross toolchain, so the vendored
# translation units get the shared header force-included instead.
if(MSVC OR CMAKE_CXX_SIMULATE_ID STREQUAL "MSVC")
    target_compile_options(xenosrecomp_core PRIVATE
        "/FI${CMAKE_CURRENT_SOURCE_DIR}/third_party/xenosrecomp/XenosRecomp/pch.h")
else()
    target_compile_options(xenosrecomp_core PRIVATE
        -include "${CMAKE_CURRENT_SOURCE_DIR}/third_party/xenosrecomp/XenosRecomp/pch.h")
endif()
target_link_libraries(onevs100 PRIVATE xenosrecomp_core xxHash::xxhash)
if(WINDOWS_STORE)
    # Translation runs at run time, so the compiler ships beside the exe.
    target_link_options(onevs100 PRIVATE "LINKER:/DELAYLOAD:dxcompiler.dll"
        "LINKER:/DELAYLOAD:dxil.dll")
    target_link_libraries(onevs100 PRIVATE delayimp)
endif()
if(ONEVS100_NATIVE_GPU AND NOT _native_gpu_without_dxc)
  if(WINDOWS_STORE)
    add_custom_command(TARGET onevs100 POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "${XENOS_RECOMP_THIRDPARTY_ROOT}/dxc-bin/bin/x64/dxcompiler.dll"
            "${XENOS_RECOMP_THIRDPARTY_ROOT}/dxc-bin/bin/x64/dxil.dll"
            "$<TARGET_FILE_DIR:onevs100>")
elseif(NOT WIN32)
    # Beside the executable, which finds them through its $ORIGIN RUNPATH.
    add_custom_command(TARGET onevs100 POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "${XENOS_RECOMP_THIRDPARTY_ROOT}/dxc-bin/lib/x64/libdxcompiler.so"
            "${XENOS_RECOMP_THIRDPARTY_ROOT}/dxc-bin/lib/x64/libdxil.so"
            "$<TARGET_FILE_DIR:onevs100>")
endif()
endif()

if(WIN32)
    # The SDK's DXBC translator, for D3D12 devices without shader model 6 (Xbox One). It lives in the xenos
    # plugin, which exports nothing, so the game compiles its own copy.
    set(_rex_graphics "${REXSDK_DIR}/src/graphics")
    add_library(onevs100_dxbc STATIC
        ${_rex_graphics}/pipeline/shader/shader.cpp
        ${_rex_graphics}/pipeline/shader/translator.cpp
        ${_rex_graphics}/pipeline/shader/translator_disasm.cpp
        ${_rex_graphics}/pipeline/shader/dxbc.cpp
        ${_rex_graphics}/pipeline/shader/dxbc_translator.cpp
        ${_rex_graphics}/pipeline/shader/dxbc_translator_alu.cpp
        ${_rex_graphics}/pipeline/shader/dxbc_translator_fetch.cpp
        ${_rex_graphics}/pipeline/shader/dxbc_translator_memexport.cpp
        ${_rex_graphics}/pipeline/shader/dxbc_translator_om.cpp
        ${_rex_graphics}/flags.cpp
        ${_rex_graphics}/format/ucode.cpp
        ${_rex_graphics}/util/draw.cpp
        ${_rex_graphics}/registers.cpp
        ${_rex_graphics}/pipeline/texture/util.cpp
        ${REXSDK_DIR}/thirdparty/dxbc/DXBCChecksum.cpp)
    target_include_directories(onevs100_dxbc PRIVATE "${REXSDK_DIR}" "${REXSDK_DIR}/thirdparty/renderdoc")
    target_link_libraries(onevs100_dxbc PUBLIC rex::runtime)
    target_link_libraries(onevs100 PRIVATE onevs100_dxbc)
    target_sources(onevs100 PRIVATE src/render/gpu_dxbc.cpp)
    target_sources(onevs100 PRIVATE src/audio_xaudio2.cpp)
    target_link_libraries(onevs100 PRIVATE xaudio2)
    target_link_options(onevs100 PRIVATE /MAP:onevs100.map)
    # Line-level profiles of the capture code: CodeView for the renderer sources only, in onevs100.pdb.
    option(ONEVS100_RENDER_DEBUG_INFO "Debug info for the native renderer sources (MSVC targets)" OFF)
    if(ONEVS100_RENDER_DEBUG_INFO)
        get_target_property(_onevs100_sources onevs100 SOURCES)
        list(FILTER _onevs100_sources INCLUDE REGEX "^src/render/")
        set_property(SOURCE ${_onevs100_sources} APPEND PROPERTY COMPILE_OPTIONS /Z7)
        target_link_options(onevs100 PRIVATE /DEBUG:FULL /OPT:REF /OPT:ICF)
    endif()
    target_compile_definitions(onevs100 PRIVATE ONEVS100_RUNTIME_DLL="$<TARGET_FILE_NAME:rex::runtime>")
else()
    # The executable's RUNPATH is $ORIGIN.
    add_custom_command(TARGET onevs100 POST_BUILD COMMAND ${CMAKE_COMMAND} -E copy_if_different
      "$<TARGET_FILE:rex::runtime>" "$<TARGET_FILE_DIR:onevs100>")
endif()

target_include_directories(onevs100 PRIVATE src/avatar_native third_party/1vs100-mm-protocol)
add_subdirectory(src/avatar_native onevs100_avatar)
target_link_libraries(onevs100 PRIVATE onevs100_avatar)

# Dead code in the title's own sources. Unused internal functions, variables and fields warn at compile time.
# Functions with external linkage that nothing calls only show at link time: a separate build configured with
# ONEVS100_REPORT_UNCALLED=ON lists every section the linker drops. That binary is for the report only.
option(ONEVS100_REPORT_UNCALLED "Link with section garbage collection and list the dropped sections" OFF)
if(CMAKE_CXX_COMPILER_ID MATCHES "Clang")
    get_target_property(_onevs100_sources onevs100 SOURCES)
    set(_onevs100_dead_code_flags -Wunused-function -Wunused-variable -Wunused-const-variable
        -Wunused-private-field -Wunused-member-function -Wunneeded-internal-declaration -Wunused-template)
    if(ONEVS100_REPORT_UNCALLED)
        list(APPEND _onevs100_dead_code_flags -ffunction-sections)
    endif()
    foreach(_source IN LISTS _onevs100_sources)
        cmake_path(ABSOLUTE_PATH _source BASE_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}" OUTPUT_VARIABLE _path)
        string(FIND "${_path}" "${CMAKE_CURRENT_SOURCE_DIR}/src/" _own)
        if(_own EQUAL 0 AND NOT _path MATCHES "third_?party" AND _path MATCHES "\\.(cpp|cc)$")
            set_property(SOURCE "${_source}" APPEND PROPERTY COMPILE_OPTIONS ${_onevs100_dead_code_flags})
        endif()
    endforeach()
    if(ONEVS100_REPORT_UNCALLED AND NOT WIN32)
        target_link_options(onevs100 PRIVATE -Wl,--gc-sections -Wl,--print-gc-sections)
    endif()
endif()
