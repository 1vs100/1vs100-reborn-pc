#include "fatal_signal_posix.h"

#include <fcntl.h>
#include <signal.h>
#include <unistd.h>

#include <SDL3/SDL_init.h>
#include <SDL3/SDL_messagebox.h>

#include <algorithm>
#include <array>
#include <string>

namespace posix_crash {
namespace {

// Signals that end the process. SIGTERM stays out: halting is not a crash.
constexpr std::array<int, 5> kFatalSignals = {SIGSEGV, SIGILL, SIGFPE, SIGBUS, SIGABRT};

// Alternate stack: guest faults often mean a broken stack.
constexpr size_t kAltStackSize = 64 * 1024;
unsigned char g_alt_stack[kAltStackSize];

constexpr size_t kDetailCapacity = kMaxDetail + 1;

char g_log_path[4096] = {};
char g_details[5][kDetailCapacity] = {};
char g_log_lines[5][32] = {};
size_t g_detail_lengths[5] = {};
size_t g_line_lengths[5] = {};
sigset_t g_unblock[5];

int IndexOf(int signal_number) {
  for (size_t i = 0; i < kFatalSignals.size(); ++i) {
    if (kFatalSignals[i] == signal_number) return int(i);
  }
  return -1;
}

void WriteAll(int fd, const char* data, size_t length) {
  size_t written = 0;
  while (written < length) {
    const ssize_t count = write(fd, data + written, length - written);
    if (count <= 0) return;
    written += size_t(count);
  }
}

// Only async-signal-safe calls here: the heap may be corrupt.
void OnFatalSignal(int signal_number) {
  const int index = IndexOf(signal_number);
  const int fd = open(g_log_path, O_WRONLY | O_CREAT | O_APPEND, 0644);
  if (fd >= 0) {
    if (index >= 0) WriteAll(fd, g_log_lines[index], g_line_lengths[index]);
    close(fd);
  }
  if (index >= 0) {
    if (SDL_WasInit(SDL_INIT_VIDEO) != 0) {
      SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_ERROR, "1 vs 100", g_details[index], nullptr);
    } else {
      WriteAll(STDERR_FILENO, g_details[index], g_detail_lengths[index]);
      WriteAll(STDERR_FILENO, "\n", 1);
    }
  }
  // SA_RESETHAND restored the default disposition; unblock first or the
  // raise pends on sa_mask. Core dumps and debuggers survive the dialog.
  if (index >= 0) sigprocmask(SIG_UNBLOCK, &g_unblock[index], nullptr);
  raise(signal_number);
  _exit(1);
}

}  // namespace

void Install(std::filesystem::path directory) {
  auto store = [](char* out, size_t capacity, const std::string& text, size_t* length) {
    *length = std::min(text.size(), capacity - 1);
    text.copy(out, *length);
    out[*length] = '\0';
  };
  size_t path_length = 0;
  store(g_log_path, sizeof(g_log_path), (directory / FaultLogName()).string(), &path_length);
  (void)path_length;
  const std::string dir = directory.string();
  for (size_t i = 0; i < kFatalSignals.size(); ++i) {
    store(g_log_lines[i], sizeof(g_log_lines[i]), LogLine(kFatalSignals[i]), &g_line_lengths[i]);
    store(g_details[i], sizeof(g_details[i]), DetailText(dir, kFatalSignals[i]), &g_detail_lengths[i]);
    sigemptyset(&g_unblock[i]);
    sigaddset(&g_unblock[i], kFatalSignals[i]);
  }
  stack_t alt = {};
  alt.ss_sp = g_alt_stack;
  alt.ss_size = sizeof(g_alt_stack);
  alt.ss_flags = 0;
  const bool have_alt = sigaltstack(&alt, nullptr) == 0;
  struct sigaction action = {};
  action.sa_handler = OnFatalSignal;
  sigemptyset(&action.sa_mask);
  for (const int sig : kFatalSignals) sigaddset(&action.sa_mask, sig);
  action.sa_flags = SA_RESETHAND | (have_alt ? SA_ONSTACK : 0);
  for (const int sig : kFatalSignals) sigaction(sig, &action, nullptr);
}

}  // namespace posix_crash
