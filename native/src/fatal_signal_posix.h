// POSIX crash capture. Installs signal handlers that log, dialog, and exit.
#pragma once

#include <cstddef>
#include <filesystem>
#include <string>

namespace posix_crash {

constexpr size_t kMaxDetail = 512;

// Fault log file name, beside the other marker files.
inline std::string FaultLogName() { return "native-fault.log"; }

// One-line log record for a fatal signal. Pure for testing.
inline std::string LogLine(int signal_number) {
  return "fatal signal=" + std::to_string(signal_number) + "\n";
}

// Dialog text for a fatal signal, naming the log directory. Pure for testing.
inline std::string DetailText(const std::string& directory, int signal_number) {
  std::string detail = "Fatal signal " + std::to_string(signal_number) + ". See " +
                       FaultLogName() + " in " + directory + ".";
  if (detail.size() > kMaxDetail) detail.resize(kMaxDetail);
  return detail;
}

// Records the directory, then installs handlers for the fatal signals.
void Install(std::filesystem::path directory);

}  // namespace posix_crash
