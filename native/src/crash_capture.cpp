// Unhandled exception capture. Writes a text description and a minidump next to
// each other, because a packaged app cannot write to its install folder.
#include <windows.h>
#include <dbghelp.h>
#include <cstdio>
#include <cwchar>

namespace {

// The user folder is writable in a package; the install folder is not.
void CrashDirectory(wchar_t* out, size_t capacity) {
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

LONG WINAPI CaptureCrash(EXCEPTION_POINTERS* info) {
  wchar_t directory[MAX_PATH];
  CrashDirectory(directory, MAX_PATH);
  SYSTEMTIME time;
  GetSystemTime(&time);
  wchar_t stem[192];
  swprintf_s(stem, 192, L"native-crash-%lu-%04u%02u%02u-%02u%02u%02u", GetCurrentProcessId(), time.wYear,
             time.wMonth, time.wDay, time.wHour, time.wMinute, time.wSecond);

  // Text first: the code and the fault address identify the site even when the
  // minidump cannot be written.
  wchar_t text_path[MAX_PATH * 2];
  swprintf_s(text_path, L"%s\\%s.txt", directory, stem);
  HANDLE text = CreateFileW(text_path, GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL,
                            nullptr);
  if (text != INVALID_HANDLE_VALUE) {
    const EXCEPTION_RECORD* record = info->ExceptionRecord;
    const uintptr_t base = reinterpret_cast<uintptr_t>(GetModuleHandleW(nullptr));
    const uintptr_t address = reinterpret_cast<uintptr_t>(record->ExceptionAddress);
    char line[512];
    int length = sprintf_s(line, "code=%08lX address=%016llX base=%016llX offset=%llX\n",
                           static_cast<unsigned long>(record->ExceptionCode),
                           static_cast<unsigned long long>(address),
                           static_cast<unsigned long long>(base),
                           static_cast<unsigned long long>(address - base));
    DWORD written = 0;
    WriteFile(text, line, DWORD(length), &written, nullptr);
    CloseHandle(text);
  }

  wchar_t dump_path[MAX_PATH * 2];
  swprintf_s(dump_path, L"%s\\%s.dmp", directory, stem);
  HANDLE file = CreateFileW(dump_path, GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL,
                            nullptr);
  if (file != INVALID_HANDLE_VALUE) {
    MINIDUMP_EXCEPTION_INFORMATION exception{GetCurrentThreadId(), info, FALSE};
    MiniDumpWriteDump(GetCurrentProcess(), GetCurrentProcessId(), file, MiniDumpNormal, &exception,
                      nullptr, nullptr);
    CloseHandle(file);
  }
  return EXCEPTION_CONTINUE_SEARCH;
}

struct InstallCapture {
  InstallCapture() {
    SetUnhandledExceptionFilter(CaptureCrash);
  }
} install_capture;

}  // namespace
