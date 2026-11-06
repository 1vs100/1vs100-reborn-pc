# Fatal dialogs spec

tl;dr: show a native error dialog before the game exits on fatal crashes and startup blockers, on Windows Desktop, UWP, Linux, and macOS.

## Goal

Every fatal exit and every startup blocker shows one user-readable dialog before the process ends. The dialog names the log or fault file. Logs keep their current content.

## Non-goals

No crash reporting upload. No change to log content or locations. No dialogs for recoverable warnings (matchmaking rejects, unsupported socket calls, content warnings). No minidump support on UWP or POSIX.

## Current state

- The only dialog is the guest terminal path in `native/src/exit_probe.cpp`. It is skipped on UWP.
- `native/src/crash_capture.cpp` (desktop Windows only) writes a text file and a minidump. No dialog.
- `native/src/death_marker.cpp` (Windows and UWP only) writes `native-fault.log` and `native-death.log` markers. No dialog.
- Linux and macOS compile neither file, so a fault there is log-only or silent.
- `guest_call.cpp` and `runtime_export.cpp` call `std::abort()` after logging. No dialog.
- Startup blockers (`Onevs100App::OnPreSetup` online-service failure, `OnFinalizePaths` missing game files, `content_install.cpp` empty content) log only.

## Design

New module `native/src/fatal_dialog.{h,cpp}` with one entry point:

```cpp
namespace fatal_dialog {
void FatalError(const char* title, const std::string& detail);
}
```

Behavior of `FatalError`:

1. Log the detail with `REXKRNL_CRITICAL` or `REXKRNL_ERROR` at the call site before calling.
2. Call `rex::FlushLogging()`.
3. Show one blocking dialog with title `1 vs 100` and the detail plus the fault or log file path.
4. Return; the caller exits or aborts as today.

Platform backends inside the module:

- Windows Desktop and Linux/macOS: `SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_ERROR, ...)`. Guard with `SDL_WasInit(SDL_INIT_VIDEO)`; when video is not up, fall back to stderr plus the log. This matches `exit_probe.cpp` today.
- UWP: never call SDL messagebox. Crash path writes `native-fault.log` and a `fatal-pending` marker in LocalState, then exits. Next launch checks the marker in `OnConfigurePaths` or `OnPostSetup` and shows an ImGui modal through the existing drawer (same stack as `AvatarDialog` and the onboarding gate). Startup errors on UWP show the same modal in-loop before exit.

POSIX crash capture is a new file `native/src/fatal_signal_posix.cpp`, compiled on non-Windows only:

- Handles `SIGSEGV`, `SIGILL`, `SIGFPE`, `SIGBUS`, and `SIGABRT`.
- Writes a short fault line with async-safe calls only (`write()`, no allocation, no logging).
- Then best-effort SDL messagebox, then `_exit(1)`.

UWP keeps `death_marker.cpp` as the fault recorder. Desktop Windows keeps `crash_capture.cpp` and adds the dialog call. POSIX gets the signal file above instead of `death_marker.cpp`.

## Wiring

| Site | Before | After |
|---|---|---|
| `exit_probe.cpp` terminal path | logs, SDL box except UWP, `_Exit(1)` | calls `FatalError` with game error code, caller address, Lua error tail, log path; UWP takes the pending-marker path |
| `crash_capture.cpp` `CaptureCrash` | writes txt plus dmp | also calls `FatalError` with the txt path |
| `death_marker.cpp` `OnFault`, `OnAbort`, `OnTerminate` | writes marker or fault log | also calls `FatalError` with the fault log path |
| `guest_call.cpp` scratch exhaustion, bad arity | `REXKRNL_CRITICAL` plus `abort` | `FatalError` then `abort` |
| `runtime_export.cpp` missing runtime or export | `REXKRNL_CRITICAL` plus `abort` | `FatalError` then `abort` |
| `onevs100_app.h` `OnPreSetup` online-service failure | `std::exit(1)` | `FatalError` with config path, then exit |
| `onevs100_app.h` `OnFinalizePaths` no game files | `REXKRNL_ERROR` only | `FatalError` with searched folder and fix steps, then exit |
| `content_install.cpp` no packages | `REXKRNL_ERROR` only | `FatalError` with content path, then return or exit per caller |

Dialog text is two lines: what failed in plain words, and where the log is. Example: `Game error 1234ABCD. See native-fault.log in userdata. Use Send logs to report it.` Exact paths resolve the same way `death_marker` does (user folder when configured, else executable folder, else `LOCALAPPDATA`).

## Safety rules

- The fault filter and the signal handler do best-effort UI only. They never allocate, lock, or log after the fault line is written.
- Dialog strings are bounded (1024 bytes) and built before the fault where possible.
- On UWP the crash path never blocks on UI. The dialog happens on the next launch.
- ASCII only in code and UI strings.

## Testing

- Manual matrix: force each path (terminal path via Lua error hook, abort via bad `GuestCall` arity in a test build, missing content folder, bad host) on Windows Desktop, Linux, and UWP. Confirm one dialog, one log line, one fault file, then exit.
- UWP: crash once, relaunch, confirm the pending-marker modal appears once and clears.
- Automated: unit test for message formatting and truncation; existing `native/tests` keep passing. No automated test for the modal filter itself.
- Review at each milestone with a fresh subagent before commit, per repo rules.

## Tickets

1. `fatal_dialog` module plus desktop wiring (exit probe, crash capture, death marker, abort sites).
2. POSIX signal handlers plus SDL box fallback.
3. UWP pending-marker plus ImGui modal.
4. Startup-blocker dialogs plus log-path text and docs touch-up.
