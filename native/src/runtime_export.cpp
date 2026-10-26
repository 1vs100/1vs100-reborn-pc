#include "runtime_export.h"
#include <rex/logging.h>
#include <rex/system/kernel_state.h>
#include <cstdlib>
#ifdef _WIN32
#include <windows.h>
#else
#include <dlfcn.h>
#endif

namespace {
PPCFunc* Resolve(const char* name) {
#ifdef _WIN32
  // ONEVS100_RUNTIME_DLL is the runtime's file name for this build configuration.
  static const HMODULE runtime = GetModuleHandleA(ONEVS100_RUNTIME_DLL);
  if (!runtime) {
    REXKRNL_CRITICAL("Runtime {} is not loaded", ONEVS100_RUNTIME_DLL);
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
    std::abort();
  }
  return function;
}
