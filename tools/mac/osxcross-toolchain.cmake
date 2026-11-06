# macOS cross build from Linux with osxcross (https://github.com/tpoechtrager/osxcross).
# Supports arm64 and x86_64 from one osxcross install; the macOS SDK itself
# comes from a tarball (Apple license forbids redistributing it, so CI
# fetches it from MACOS_SDK_URL and caches the built toolchain).
#
# Environment:
#   OSXCROSS_ROOT            osxcross install dir (default ~/.local/share/osxcross)
#   MAC_ARCH                 arm64 or x86_64 (required)
#   MACOS_DEPLOYMENT_TARGET  oldest supported macOS (default 14.0, matches Info.plist)

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
list(LENGTH _osx_cc _osx_cc_count)
if(NOT _osx_cc_count EQUAL 1)
  message(FATAL_ERROR "Expected one $ENV{MAC_ARCH}-apple-darwin*-clang under ${_osx_root}/bin, found ${_osx_cc_count}")
endif()
list(GET _osx_cc 0 CMAKE_C_COMPILER)
file(GLOB _osx_cxx "${_osx_root}/bin/$ENV{MAC_ARCH}-apple-darwin*-clang++")
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

if(NOT DEFINED ENV{MACOS_DEPLOYMENT_TARGET})
  set(ENV{MACOS_DEPLOYMENT_TARGET} "14.0")
endif()
set(CMAKE_OSX_DEPLOYMENT_TARGET "$ENV{MACOS_DEPLOYMENT_TARGET}" CACHE STRING "" FORCE)
set(CMAKE_OSX_ARCHITECTURES "$ENV{MAC_ARCH}" CACHE STRING "" FORCE)

file(GLOB _osx_sdks "${_osx_root}/SDK/MacOSX*.sdk")
list(LENGTH _osx_sdks _osx_sdk_count)
if(NOT _osx_sdk_count EQUAL 1)
  message(FATAL_ERROR "Expected one MacOSX*.sdk under ${_osx_root}/SDK, found ${_osx_sdk_count}")
endif()
list(GET _osx_sdks 0 _osx_sdk)
set(CMAKE_OSX_SYSROOT "${_osx_sdk}" CACHE PATH "" FORCE)
