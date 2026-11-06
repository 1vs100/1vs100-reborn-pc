// Synchronous death markers. The log sink flushes every few seconds, so a
// silent process death leaves no trace in the logs. These handlers write one
// line with raw file calls to marker files in the configured user folder: a
// clean shutdown writes "exit"; any other marker (or none) classifies the
// death.
#include <windows.h>

#include <algorithm>
#include <array>
#include <chrono>
#include <map>
#include <csignal>
#include <cstdio>
#include <cstring>
#include <exception>
#include <string>
#include <thread>
#include <vector>

#include "death_marker.h"
#include "fatal_dialog.h"

#include <rex/ppc/context.h>
#include <rex/system/kernel_state.h>
#include <rex/system/thread_state.h>
#include <rex/system/xthread.h>

#include "render/display_settings.h"

namespace {
LPTOP_LEVEL_EXCEPTION_FILTER g_previous_fault_filter = nullptr;

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

// Module and offset of a code address: on a console there is no debugger to name a system DLL from its address.
void WriteFrame(FILE* file, const wchar_t* tag, uintptr_t address) {
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
}

// Uploading dump-threads requests a thread dump, profile-threads a 20 s sampling profile.
std::wstring TriggerPath(const wchar_t* name = L"dump-threads") {
  wchar_t directory[MAX_PATH];
  DeathDirectory(directory, MAX_PATH);
  return std::wstring(directory) + L"\\" + name;
}

FILE* OpenFaultLog() {
  wchar_t directory[MAX_PATH];
  DeathDirectory(directory, MAX_PATH);
  wchar_t path[MAX_PATH * 2];
  swprintf_s(path, L"%s\\native-fault.log", directory);
  FILE* file = nullptr;
  _wfopen_s(&file, path, L"a");
  return file;
}

// Host frames of a suspended thread, innermost first; also the nearest frame inside `module` when given. A thread
// stopped at an arbitrary instruction can hand the unwinder a context it faults on, so the walk is guarded and a
// fault just ends it. No C++ objects live here: __try needs a frame without destructors.
int UnwindThread(CONTEXT context, uintptr_t* frames, int max, uintptr_t module, uintptr_t* module_frame) {
  int count = 0;
  __try {
    for (; count < max && context.Rip; ++count) {
      if (frames) frames[count] = uintptr_t(context.Rip);
      DWORD64 image_base = 0;
      auto* entry = RtlLookupFunctionEntry(context.Rip, &image_base, nullptr);
      if (module && image_base == module) {
        *module_frame = uintptr_t(context.Rip);
        return count + 1;
      }
      if (!entry) {
        // Only the innermost frame can be a leaf; elsewhere Rsp is not trustworthy.
        if (count) return count + 1;
        context.Rip = *reinterpret_cast<DWORD64*>(context.Rsp);
        context.Rsp += 8;
        continue;
      }
      void* handler_data = nullptr;
      DWORD64 establisher = 0;
      RtlVirtualUnwind(UNW_FLAG_NHANDLER, image_base, context.Rip, entry, &context, &handler_data, &establisher,
                       nullptr);
    }
  } __except (EXCEPTION_EXECUTE_HANDLER) {
  }
  return count;
}

// Every guest thread's registers and host stack, for a guest that stopped presenting. Each thread is suspended only
// while its context is copied and unwound; the writes happen after it resumes.
void WriteStall(const wchar_t* reason) {
  auto* kernel = rex::system::kernel_state();
  if (!kernel) return;
  struct Sample {
    uint32_t id = 0, lr = 0, r1 = 0, r3 = 0, r31 = 0;
    std::string name;
    DWORD host = 0;
    uintptr_t frames[16] = {};
    int count = 0;
  };
  std::vector<Sample> samples;
  for (auto& thread : kernel->object_table()->GetObjectsByType<rex::system::XThread>()) {
    if (!thread->is_guest_thread() || !thread->thread_state() || !thread->thread()) continue;
    Sample s;
    const PPCContext& c = *thread->thread_state()->context();
    s.id = thread->thread_id();
    s.name = thread->name();
    HANDLE handle = static_cast<HANDLE>(thread->thread()->native_handle());
    s.host = GetThreadId(handle);
    // The lookup can block if the suspended thread holds the loader lock; accepted for a diagnostic.
    if (SuspendThread(handle) != DWORD(-1)) {
      s.lr = uint32_t(c.lr);
      s.r1 = c.r1.u32, s.r3 = c.r3.u32, s.r31 = c.r31.u32;
      CONTEXT context{};
      context.ContextFlags = CONTEXT_CONTROL | CONTEXT_INTEGER;
      if (GetThreadContext(handle, &context)) {
        s.count = UnwindThread(context, s.frames, 16, 0, nullptr);
      }
      ResumeThread(handle);
    }
    samples.push_back(s);
  }
  FILE* file = OpenFaultLog();
  if (!file) return;
  fwprintf(file, L"%s\n", reason);
  for (const auto& s : samples) {
    fwprintf(file, L"thread %08X host=%lu %S lr=%08X r1=%08X r3=%08X r31=%08X\n", s.id, s.host, s.name.c_str(), s.lr, s.r1, s.r3, s.r31);
    for (int i = 0; i < s.count; ++i) WriteFrame(file, i ? L"at" : L"pc", s.frames[i]);
  }
  fclose(file);
}

// Samples every guest thread's instruction pointer for a while and writes hit counts per thread and code location,
// the innermost frame and the nearest frame in the executable (the recompiled guest function).
void WriteProfile(int seconds) {
  auto* kernel = rex::system::kernel_state();
  if (!kernel) return;
  // The innermost frame and the four nearest frames in the executable (recompiled guest functions and the renderer).
  using Key = std::array<uintptr_t, 5>;
  struct Target {
    HANDLE handle;
    uint32_t id;
    std::string name;
    uint64_t samples = 0;
    std::map<Key, uint64_t> hits;
  };
  std::vector<Target> targets;
  for (auto& thread : kernel->object_table()->GetObjectsByType<rex::system::XThread>()) {
    if (!thread->is_guest_thread() || !thread->thread()) continue;
    targets.push_back({static_cast<HANDLE>(thread->thread()->native_handle()), thread->thread_id(), thread->name()});
  }
  const auto exe = reinterpret_cast<uintptr_t>(GetModuleHandleW(nullptr));
  const auto* dos = reinterpret_cast<const IMAGE_DOS_HEADER*>(exe);
  const auto* nt = reinterpret_cast<const IMAGE_NT_HEADERS*>(exe + dos->e_lfanew);
  const uintptr_t exe_end = exe + nt->OptionalHeader.SizeOfImage;
  const auto end = std::chrono::steady_clock::now() + std::chrono::seconds(seconds);
  uintptr_t frames[24];
  while (std::chrono::steady_clock::now() < end) {
    for (auto& target : targets) {
      if (SuspendThread(target.handle) == DWORD(-1)) continue;
      CONTEXT context{};
      context.ContextFlags = CONTEXT_CONTROL | CONTEXT_INTEGER;
      int count = 0;
      if (GetThreadContext(target.handle, &context)) count = UnwindThread(context, frames, 24, 0, nullptr);
      ResumeThread(target.handle);
      Key key{};
      if (count) key[0] = frames[0];
      for (int i = 0, n = 1; i < count && n < 5; ++i) {
        if (frames[i] >= exe && frames[i] < exe_end) key[n++] = frames[i];
      }
      ++target.samples;
      ++target.hits[key];
    }
    Sleep(2);
  }
  wchar_t directory[MAX_PATH];
  DeathDirectory(directory, MAX_PATH);
  FILE* file = nullptr;
  _wfopen_s(&file, (std::wstring(directory) + L"\\native-profile.log").c_str(), L"w");
  if (!file) return;
  fwprintf(file, L"profile: %d s, executable base %llX\n", seconds, static_cast<unsigned long long>(exe));
  for (auto& target : targets) {
    fwprintf(file, L"thread %08X %S samples=%llu\n", target.id, target.name.c_str(),
             static_cast<unsigned long long>(target.samples));
    std::vector<std::pair<uint64_t, Key>> sorted;
    for (const auto& [key, count] : target.hits) sorted.push_back({count, key});
    std::sort(sorted.rbegin(), sorted.rend());
    for (const auto& [count, key] : sorted) {
      fwprintf(file, L"  %llu", static_cast<unsigned long long>(count));
      WriteFrame(file, L"", key[0]);
      fwprintf(file, L"    exe");
      for (size_t i = 1; i < key.size() && key[i]; ++i) fwprintf(file, L" %llX", static_cast<unsigned long long>(key[i] - exe));
      fwprintf(file, L"\n");
    }
  }
  fclose(file);
}

void OnAbort(int) {
  DeathMarker("abort\n");
  fatal_dialog::FatalError("1 vs 100", "Aborted. See native-fault.log in the user folder.");
  signal(SIGABRT, SIG_DFL);
  raise(SIGABRT);
}

void OnTerminate() {
  DeathMarker("terminate\n");
  fatal_dialog::FatalError("1 vs 100", "Terminated. See native-fault.log in the user folder.");
  abort();
}

void OnExit() {
  DeathMarker("exit\n");
}

// OnFault stays log-only and never blocks on UI. The unhandled dialog
// lives in crash_capture on desktop and in the pending-marker modal on UWP.

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
    if (g_previous_fault_filter != nullptr) return g_previous_fault_filter(info);
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
  // The guest registers, when the faulting thread runs guest code; r13 is its PCR, which holds the stack bounds.
  if (auto* state = rex::runtime::ThreadState::Get()) {
    const PPCContext& c = *state->context();
    const PPCRegister* r[32] = {&c.r0,  &c.r1,  &c.r2,  &c.r3,  &c.r4,  &c.r5,  &c.r6,  &c.r7,
                                &c.r8,  &c.r9,  &c.r10, &c.r11, &c.r12, &c.r13, &c.r14, &c.r15,
                                &c.r16, &c.r17, &c.r18, &c.r19, &c.r20, &c.r21, &c.r22, &c.r23,
                                &c.r24, &c.r25, &c.r26, &c.r27, &c.r28, &c.r29, &c.r30, &c.r31};
    fwprintf(file, L"  guest lr=%08X\n", static_cast<unsigned>(c.lr));
    for (int i = 0; i < 32; i += 8) {
      fwprintf(file, L"  r%-2d %08X %08X %08X %08X %08X %08X %08X %08X\n", i, r[i]->u32, r[i + 1]->u32, r[i + 2]->u32,
               r[i + 3]->u32, r[i + 4]->u32, r[i + 5]->u32, r[i + 6]->u32, r[i + 7]->u32);
    }
    const uint32_t pcr = c.r13.u32;
    if (pcr >= 0x10000 && pcr < 0xFFFF0000u) {
      const auto* words = REX_KERNEL_MEMORY()->TranslateVirtual<const uint8_t*>(pcr + 0x70);
      MEMORY_BASIC_INFORMATION region{};
      if (VirtualQuery(words, &region, sizeof(region)) && region.State == MEM_COMMIT) {
        uint32_t base = 0, end = 0;
        std::memcpy(&base, words, 4);
        std::memcpy(&end, words + 4, 4);
        fwprintf(file, L"  guest stack %08X..%08X\n", _byteswap_ulong(end), _byteswap_ulong(base));
      }
    }
  }
  frame(L"pc", pc);
  void* stack[32];
  const USHORT count = RtlCaptureStackBackTrace(0, 32, stack, nullptr);
  for (USHORT i = 0; i < count; ++i) frame(L"at", reinterpret_cast<uintptr_t>(stack[i]));
  fclose(file);
  if (g_previous_fault_filter != nullptr) return g_previous_fault_filter(info);
  return EXCEPTION_CONTINUE_SEARCH;
}

struct InstallDeathMarkers {
  InstallDeathMarkers() {
    g_previous_fault_filter = SetUnhandledExceptionFilter(OnFault);
    signal(SIGABRT, OnAbort);
    std::set_terminate(OnTerminate);
    atexit(OnExit);
    // Wall-clock heartbeat, independent of the log sink: if the guest wedges
    // but the process lives, ticks keep arriving in native-death.log.
    std::thread([] {
      uint64_t last = 0;
      int still = 0;
      for (;;) {
        Sleep(5000);
        DeathMarker("tick\n");
        const uint64_t frames = onevs100::render::display::FrameCount();
        still = frames && frames == last ? still + 1 : 0;
        last = frames;
        if (still == 2) WriteStall(L"stall: no guest frame for 10 s");
        if (DeleteFileW(TriggerPath().c_str())) {
          WriteStall(L"dump requested");
          DeathMarker("dump written\n");
        }
        if (DeleteFileW(TriggerPath(L"profile-threads").c_str())) std::thread(WriteProfile, 20).detach();
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
