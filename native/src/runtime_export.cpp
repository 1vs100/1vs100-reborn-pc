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
  HMODULE runtime = nullptr;
  GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                     reinterpret_cast<LPCWSTR>(&rex::system::kernel_state), &runtime);
  return runtime ? reinterpret_cast<PPCFunc*>(GetProcAddress(runtime, name)) : nullptr;
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
