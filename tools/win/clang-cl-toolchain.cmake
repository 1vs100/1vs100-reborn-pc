# Windows (Win32/desktop) cross build from Linux with clang-cl and lld-link.
#
# Headers and libraries come from an xwin splat (XWIN_ROOT) of the Windows SDK
# and the MSVC CRT. The SDK's own CI presets compile with -march=x86-64-v2;
# clang-cl defaults to SSE2, so the TU-level SSE4.1 flag is set here, along
# with the char8_t opt-out the SDK takes for granted (its GNU spelling is
# ignored by clang-cl). Mirrors tools/uwp/clang-cl-toolchain.cmake, which
# additionally uses the MSVC STL and the Store CRT.
#
# Environment:
#   XWIN_ROOT  xwin splat directory (default ~/.cache/cargo-xwin/xwin)

set(CMAKE_SYSTEM_NAME Windows)
set(CMAKE_SYSTEM_PROCESSOR AMD64)
set(CMAKE_C_COMPILER clang-cl)
set(CMAKE_CXX_COMPILER clang-cl)
set(CMAKE_LINKER lld-link)
set(CMAKE_RC_COMPILER llvm-rc)
set(CMAKE_MT llvm-mt)
set(CMAKE_AR llvm-lib)

if(NOT DEFINED ENV{XWIN_ROOT})
  set(ENV{XWIN_ROOT} "$ENV{HOME}/.cache/cargo-xwin/xwin")
endif()
set(X "$ENV{XWIN_ROOT}")

# Source files include a few SDK headers with a case the Linux file system
# does not find.
set(SHIM "${CMAKE_BINARY_DIR}/caseshim")
file(MAKE_DIRECTORY "${SHIM}")
file(CREATE_LINK "${X}/sdk/include/um/objbase.h" "${SHIM}/ObjBase.h" SYMBOLIC)
file(CREATE_LINK "${X}/sdk/include/shared/sdkddkver.h" "${SHIM}/SDKDDKVer.h" SYMBOLIC)

set(INC "/imsvc${SHIM} /imsvc ${X}/crt/include /imsvc ${X}/sdk/include/ucrt /imsvc ${X}/sdk/include/um /imsvc ${X}/sdk/include/shared /imsvc ${X}/sdk/include/winrt")
set(CMAKE_C_FLAGS_INIT "${INC} /clang:-msse4.1 /clang:-fno-char8_t")
set(CMAKE_CXX_FLAGS_INIT "${INC} /clang:-msse4.1 /clang:-fno-char8_t")
set(LIBS "/libpath:${X}/crt/lib/x86_64 /libpath:${X}/sdk/lib/um/x86_64 /libpath:${X}/sdk/lib/ucrt/x86_64")
set(CMAKE_EXE_LINKER_FLAGS_INIT "${LIBS}")
set(CMAKE_SHARED_LINKER_FLAGS_INIT "${LIBS}")
set(CMAKE_FIND_ROOT_PATH_MODE_PACKAGE BOTH)
set(CMAKE_TRY_COMPILE_CONFIGURATION Release)
set(CMAKE_MSVC_RUNTIME_LIBRARY MultiThreadedDLL)
