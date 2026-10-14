#include <windows.h>
#include <dbghelp.h>
#include <cstdio>

namespace {
LONG WINAPI CaptureCrash(EXCEPTION_POINTERS* info) {
  wchar_t name[128];
  SYSTEMTIME time;
  GetSystemTime(&time);
  swprintf_s(name, L"native-crash-%lu-%04u%02u%02u-%02u%02u%02u.dmp",
      GetCurrentProcessId(), time.wYear, time.wMonth, time.wDay,
      time.wHour, time.wMinute, time.wSecond);
  HANDLE file = CreateFileW(name, GENERIC_WRITE, 0, nullptr, CREATE_NEW,
      FILE_ATTRIBUTE_NORMAL, nullptr);
  if (file != INVALID_HANDLE_VALUE) {
    MINIDUMP_EXCEPTION_INFORMATION exception{GetCurrentThreadId(), info, FALSE};
    MiniDumpWriteDump(GetCurrentProcess(), GetCurrentProcessId(), file,
        MiniDumpNormal, &exception, nullptr, nullptr);
    CloseHandle(file);
  }
  return EXCEPTION_CONTINUE_SEARCH;
}
struct InstallCapture {
  InstallCapture() { SetUnhandledExceptionFilter(CaptureCrash); }
} installCapture;
}
