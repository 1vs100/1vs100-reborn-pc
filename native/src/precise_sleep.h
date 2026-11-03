#pragma once

#include <chrono>
#include <thread>

#if defined(_WIN32)
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#endif

namespace onevs100::thread {

// Plain Sleep rounds to the ~15.6 ms timer tick, which is too coarse for a
// 1 ms sweep. A high-resolution waitable timer lands within ~0.5 ms.
inline void SleepPrecise(std::chrono::microseconds duration) {
#if defined(_WIN32)
  // One timer per calling thread, created lazily. Needs Win10 1803+.
  struct TimerHolder {
    HANDLE handle;
    TimerHolder()
        : handle(CreateWaitableTimerExW(nullptr, nullptr, CREATE_WAITABLE_TIMER_HIGH_RESOLUTION,
                                       TIMER_ALL_ACCESS)) {}
    ~TimerHolder() {
      if (handle) CloseHandle(handle);
    }
  };
  thread_local TimerHolder timer;
  if (timer.handle) {
    LARGE_INTEGER due_time;
    due_time.QuadPart = -static_cast<LONGLONG>(duration.count()) * 10;  // relative, 100ns units
    if (SetWaitableTimer(timer.handle, &due_time, 0, nullptr, nullptr, FALSE) &&
        WaitForSingleObject(timer.handle, INFINITE) == WAIT_OBJECT_0) {
      return;
    }
  }
  std::this_thread::sleep_for(duration);
#else
  // nanosleep already resolves to well under a millisecond.
  std::this_thread::sleep_for(duration);
#endif
}

}  // namespace onevs100::thread
