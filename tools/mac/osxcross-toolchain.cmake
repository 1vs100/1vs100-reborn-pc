# macOS cross build from Linux with osxcross (https://github.com/tpoechtrager/osxcross).
# The macOS SDK itself comes from your own tarball: Apple forbids
# redistributing it, so the cross route is local-only and never runs in CI.
#
# Environment:
#   OSXCROSS_ROOT            osxcross install dir (default ~/.local/share/osxcross)
#   MAC_ARCH                 arm64 or x86_64 (required)
#   MACOS_DEPLOYMENT_TARGET  oldest supported macOS (default from minimum_os.txt)

set(CMAKE_SYSTEM_NAME Darwin)
set(CMAKE_SYSTEM_PROCESSOR "$ENV{MAC_ARCH}")

if(NOT DEFINED ENV{OSXCROSS_ROOT})
  set(ENV{OSXCROSS_ROOT} "$ENV{HOME}/.local/share/osxcross")
endif()
set(_osx_root "$ENV{OSXCROSS_ROOT}/target")

if(NOT "$ENV{MAC_ARCH}" MATCHES "^(arm64|x86_64)$")
  message(FATAL_ERROR "MAC_ARCH must be arm64 or x86_64, got '$ENV{MAC_ARCH}'")
endif()

file(GLOB _osx_cc "${_osx_root}/bin/$ENV{MAC_ARCH}-apple-darwin*-clang")
list(FILTER _osx_cc EXCLUDE REGEX "cmake-clang$")
list(LENGTH _osx_cc _osx_cc_count)
if(NOT _osx_cc_count EQUAL 1)
  message(FATAL_ERROR "Expected one $ENV{MAC_ARCH}-apple-darwin*-clang under ${_osx_root}/bin, found ${_osx_cc_count}")
endif()
list(GET _osx_cc 0 CMAKE_C_COMPILER)
file(GLOB _osx_cxx "${_osx_root}/bin/$ENV{MAC_ARCH}-apple-darwin*-clang++")
list(FILTER _osx_cxx EXCLUDE REGEX "cmake-clang\\+\\+$")
list(LENGTH _osx_cxx _osx_cxx_count)
if(NOT _osx_cxx_count EQUAL 1)
  message(FATAL_ERROR "Expected one $ENV{MAC_ARCH}-apple-darwin*-clang++ under ${_osx_root}/bin, found ${_osx_cxx_count}")
endif()
list(GET _osx_cxx 0 CMAKE_CXX_COMPILER)
string(REGEX REPLACE "-clang(\\+\\+)?$" "" _osx_triple "${CMAKE_C_COMPILER}")
get_filename_component(_osx_triple "${_osx_triple}" NAME)
foreach(_osx_tool IN ITEMS ar ranlib)
  if(NOT EXISTS "${_osx_root}/bin/${_osx_triple}-${_osx_tool}")
    message(FATAL_ERROR "Missing ${_osx_root}/bin/${_osx_triple}-${_osx_tool}")
  endif()
endforeach()
set(CMAKE_AR "${_osx_root}/bin/${_osx_triple}-ar")
set(CMAKE_RANLIB "${_osx_root}/bin/${_osx_triple}-ranlib")
# Upstream clang ships no macOS compiler runtime (___isPlatformVersionAtLeast
# and friends); use the osxcross-built runtimes. Build the overlay with the
# steps in tools/mac/README.md; without it, linking fails.
if(DEFINED ENV{OSXCROSS_CLANG_RES})
  set(_osx_resdir "$ENV{OSXCROSS_CLANG_RES}")
else()
  # Newest assembled overlay wins; see tools/mac/README.md.
  file(GLOB _osx_resdirs "$ENV{HOME}/.local/share/osxcross/clang-res/*")
  list(SORT _osx_resdirs COMPARE NATURAL)
  if(_osx_resdirs)
    list(GET _osx_resdirs -1 _osx_resdir)
  else()
    set(_osx_resdir "")
  endif()
endif()
if(EXISTS "${_osx_resdir}/lib/darwin/libclang_rt.osx.a")
  add_link_options(-resource-dir=${_osx_resdir})
else()
  message(WARNING "macOS compiler-rt overlay missing at ${_osx_resdir}; see tools/mac/README.md")
endif()
# Upstream clang would otherwise pick the host linker. Point it at the
# pinned cctools ld64 from this osxcross install instead.
set(_osx_linker "${_osx_root}/bin/${_osx_triple}-ld")
if(NOT EXISTS "${_osx_linker}")
  message(FATAL_ERROR "Missing ${_osx_linker}")
endif()
set(_osx_link_flags "-fuse-ld=${_osx_linker}")
set(CMAKE_EXE_LINKER_FLAGS_INIT "${_osx_link_flags}")
set(CMAKE_SHARED_LINKER_FLAGS_INIT "${_osx_link_flags}")
set(CMAKE_MODULE_LINKER_FLAGS_INIT "${_osx_link_flags}")

if(NOT DEFINED ENV{MACOS_DEPLOYMENT_TARGET})
  file(READ "${CMAKE_CURRENT_LIST_DIR}/minimum_os.txt" _osx_min)
  string(STRIP "${_osx_min}" _osx_min)
  if(NOT _osx_min MATCHES "^[0-9]+\\.[0-9]+$")
    message(FATAL_ERROR "Bad version in tools/mac/minimum_os.txt: '${_osx_min}'")
  endif()
  set(ENV{MACOS_DEPLOYMENT_TARGET} "${_osx_min}")
endif()
set(CMAKE_OSX_DEPLOYMENT_TARGET "$ENV{MACOS_DEPLOYMENT_TARGET}" CACHE STRING "Oldest supported macOS" FORCE)
set(CMAKE_OSX_ARCHITECTURES "$ENV{MAC_ARCH}" CACHE STRING "" FORCE)

file(GLOB _osx_sdks "${_osx_root}/SDK/MacOSX*.sdk")
list(LENGTH _osx_sdks _osx_sdk_count)
if(NOT _osx_sdk_count EQUAL 1)
  message(FATAL_ERROR "Expected one MacOSX*.sdk under ${_osx_root}/SDK, found ${_osx_sdk_count}")
endif()
list(GET _osx_sdks 0 _osx_sdk)
set(CMAKE_OSX_SYSROOT "${_osx_sdk}" CACHE PATH "" FORCE)

# Keep host .pc files (Linux .so) out of the macOS build. Without this,
# pkg-config resolves host libraries such as libusb and SDL tries to link
# them into a macOS binary. The osxcross prefix has no .pc files installed,
# so optional backends disable cleanly instead.
set(ENV{PKG_CONFIG_LIBDIR} "${_osx_root}/macports/pkgs/opt/local/lib/pkgconfig")
set(ENV{PKG_CONFIG_SYSROOT_DIR} "${_osx_root}/macports/pkgs")

# Keep host libraries and headers out of the link and include lines. SDL's
# HIDAPI check, for example, resolves host libusb otherwise and then fails
# its own dylib check. Programs and packages stay unrestricted: build tools
# run on the host, and config packages arrive by absolute path.
set(CMAKE_FIND_ROOT_PATH "${CMAKE_OSX_SYSROOT}")
set(CMAKE_FIND_ROOT_PATH_MODE_PROGRAM NEVER)
set(CMAKE_FIND_ROOT_PATH_MODE_LIBRARY ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_INCLUDE ONLY)
