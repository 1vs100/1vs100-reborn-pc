#include "runtime_export.h"
#include "fatal_dialog.h"
#include <rex/logging.h>
#include <rex/platform.h>
#include <rex/system/kernel_state.h>
#include <cstdlib>
#include <string>
#ifdef _WIN32
#include <windows.h>
#else
#include <dlfcn.h>
#endif

namespace {
PPCFunc* Resolve(const char* name) {
#if REX_PLATFORM_WINRT
  // UWP has no GetModuleHandle. The runtime is part of the package and already
  // loaded, so LoadPackagedLibrary returns its handle.
  static const HMODULE runtime = [] {
    const std::string name = ONEVS100_RUNTIME_DLL;
    return LoadPackagedLibrary(std::wstring(name.begin(), name.end()).c_str(), 0);
  }();
  if (!runtime) {
    REXKRNL_CRITICAL("Runtime {} is not loaded", ONEVS100_RUNTIME_DLL);
    if (fatal_dialog::ClaimFatal())
      fatal_dialog::FatalError("1 vs 100", "Runtime library is not loaded. See native-fault.log in the user folder.");
    std::abort();
  }
  return reinterpret_cast<PPCFunc*>(GetProcAddress(runtime, name));
#elif defined(_WIN32)
  // ONEVS100_RUNTIME_DLL is the runtime's file name for this build configuration.
  static const HMODULE runtime = GetModuleHandleA(ONEVS100_RUNTIME_DLL);
  if (!runtime) {
    REXKRNL_CRITICAL("Runtime {} is not loaded", ONEVS100_RUNTIME_DLL);
    if (fatal_dialog::ClaimFatal())
      fatal_dialog::FatalError("1 vs 100", "Runtime library is not loaded. See native-fault.log in the user folder.");
    std::abort();
  }
  return reinterpret_cast<PPCFunc*>(GetProcAddress(runtime, name));
#else
  // The executable's override comes first in symbol lookup; the next one is the runtime's.
  return reinterpret_cast<PPCFunc*>(dlsym(RTLD_NEXT, name));
#endif
}
}  // namespace

PPCFunc* RuntimeExport(const char* name) {
  PPCFunc* const function = Resolve(name);
  if (!function) {
    REXKRNL_CRITICAL("Runtime export {} not found", name);
    if (fatal_dialog::ClaimFatal())
      fatal_dialog::FatalError("1 vs 100", std::string("Runtime export not found: ") + name +
                                              ". See native-fault.log in the user folder.");
    std::abort();
  }
  return function;
}
