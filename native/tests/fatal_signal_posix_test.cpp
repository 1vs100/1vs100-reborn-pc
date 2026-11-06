// Message and log-line bounds for the POSIX crash handler.
#include <csignal>
#include <cstdio>
#include <string>

#include "fatal_signal_posix.h"

int main() {
  if (posix_crash::FaultLogName() != "native-fault.log") {
    std::printf("FAIL: log name changed\n");
    return 1;
  }
  const std::string expected_line = "fatal signal=" + std::to_string(SIGSEGV) + "\n";
  if (posix_crash::LogLine(SIGSEGV) != expected_line) {
    std::printf("FAIL: log line wrong\n");
    return 1;
  }
  const std::string detail = posix_crash::DetailText("/game/userdata", SIGSEGV);
  if (detail.find("/game/userdata") == std::string::npos ||
      detail.find("native-fault.log") == std::string::npos ||
      detail.find(std::to_string(SIGSEGV)) == std::string::npos) {
    std::printf("FAIL: detail misses directory, file, or signal\n");
    return 1;
  }
  const std::string far_dir(2048, 'd');
  if (posix_crash::DetailText(far_dir, SIGABRT).size() != posix_crash::kMaxDetail) {
    std::printf("FAIL: long detail not cut to %zu\n", posix_crash::kMaxDetail);
    return 1;
  }
  std::printf("PASS: POSIX crash text names the log and signal\n");
  return 0;
}
