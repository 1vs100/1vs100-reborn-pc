// Synchronous death markers. The log sink flushes every few seconds, so a
// silent process death leaves no trace in the logs. These handlers write one
// line with raw file calls to marker files in the configured user folder: a
// clean shutdown writes "exit"; any other marker (or none) classifies the
// death.
#include <windows.h>

#include <csignal>
#include <cstdio>
#include <cstring>
#include <exception>
#include <thread>

#include "death_marker.h"

namespace {

std::filesystem::path g_directory;

void DeathDirectory(wchar_t* out, size_t capacity) {
  if (!g_directory.empty()) {
    wcsncpy_s(out, capacity, g_directory.c_str(), _TRUNCATE);
    return;
  }
  DWORD length = GetEnvironmentVariableW(L"LOCALAPPDATA", out, DWORD(capacity));
  if (length > 0 && length < capacity) {
    return;
  }
  GetModuleFileNameW(nullptr, out, DWORD(capacity));
  wchar_t* slash = wcsrchr(out, L'\\');
  if (slash != nullptr) {
    slash[1] = 0;
  }
}

void DeathMarker(const char* word) {
  wchar_t directory[MAX_PATH];
  DeathDirectory(directory, MAX_PATH);
  wchar_t path[MAX_PATH * 2];
  swprintf_s(path, L"%s\\native-death.log", directory);
  FILE* file = nullptr;
  _wfopen_s(&file, path, L"a");
  if (file == nullptr) {
    return;
  }
  fwrite(word, 1, strlen(word), file);
  fclose(file);
}

void OnAbort(int) {
  DeathMarker("abort\n");
  signal(SIGABRT, SIG_DFL);
  raise(SIGABRT);
}

void OnTerminate() {
  DeathMarker("terminate\n");
  abort();
}

void OnExit() {
  DeathMarker("exit\n");
}

// First-chance fault record. UWP builds exclude the minidump capture, so a
// guest access violation would otherwise leave no trace; this writes the
// fault code, the accessed address and the faulting PC with raw file calls.
LONG WINAPI OnFault(EXCEPTION_POINTERS* info) {
  const auto* record = info->ExceptionRecord;
  const uintptr_t pc = reinterpret_cast<uintptr_t>(info->ContextRecord->Rip);
  uintptr_t access = 0;
  if (record->NumberParameters > 1) {
    access = static_cast<uintptr_t>(record->ExceptionInformation[1]);
  }
  wchar_t directory[MAX_PATH];
  DeathDirectory(directory, MAX_PATH);
  wchar_t path[MAX_PATH * 2];
  swprintf_s(path, L"%s\\native-fault.log", directory);
  FILE* file = nullptr;
  _wfopen_s(&file, path, L"a");
  if (file == nullptr) {
    return EXCEPTION_CONTINUE_SEARCH;
  }
  fwprintf(file, L"base=%016llX code=%08lX access=%016llX pc=%016llX\n",
           static_cast<unsigned long long>(reinterpret_cast<uintptr_t>(GetModuleHandleW(nullptr))),
           static_cast<unsigned long>(record->ExceptionCode), static_cast<unsigned long long>(access),
           static_cast<unsigned long long>(pc));
  // Module and offset of the fault and its callers: on a console there is no debugger to
  // name a system DLL from its address. The filter runs on the faulting stack.
  const auto frame = [&](const wchar_t* tag, uintptr_t address) {
    MEMORY_BASIC_INFORMATION region{};
    wchar_t module[MAX_PATH] = L"?";
    uintptr_t module_base = 0;
    if (VirtualQuery(reinterpret_cast<void*>(address), &region, sizeof(region))) {
      module_base = reinterpret_cast<uintptr_t>(region.AllocationBase);
      GetModuleFileNameW(static_cast<HMODULE>(region.AllocationBase), module, MAX_PATH);
    }
    const wchar_t* name = wcsrchr(module, L'\\');
    fwprintf(file, L"  %s %s+%llX\n", tag, name ? name + 1 : module,
             static_cast<unsigned long long>(address - module_base));
  };
  frame(L"pc", pc);
  void* stack[32];
  const USHORT count = RtlCaptureStackBackTrace(0, 32, stack, nullptr);
  for (USHORT i = 0; i < count; ++i) frame(L"at", reinterpret_cast<uintptr_t>(stack[i]));
  fclose(file);
  return EXCEPTION_CONTINUE_SEARCH;
}

struct InstallDeathMarkers {
  InstallDeathMarkers() {
    SetUnhandledExceptionFilter(OnFault);
    signal(SIGABRT, OnAbort);
    std::set_terminate(OnTerminate);
    atexit(OnExit);
    // Wall-clock heartbeat, independent of the log sink: if the guest wedges
    // but the process lives, ticks keep arriving in native-death.log.
    std::thread([] {
      for (;;) {
        Sleep(5000);
        DeathMarker("tick\n");
      }
    }).detach();
  }
} install_death_markers;

}  // namespace

namespace death_marker {

void SetDirectory(std::filesystem::path directory) {
  g_directory = std::move(directory);
}

}  // namespace death_marker
