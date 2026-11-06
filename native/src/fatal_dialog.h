// Blocking fatal-error dialog. Call sites log first; this flushes and shows.
#pragma once

#include <cstddef>
#include <filesystem>
#include <fstream>
#include <optional>
#include <string>
#include <utility>

namespace fatal_dialog {

constexpr size_t kMaxDetail = 1024;

// Fault paths cannot trust long strings; bound what reaches the dialog.
inline std::string TruncateDetail(const std::string& detail) {
  if (detail.size() <= kMaxDetail) return detail;
  return detail.substr(0, kMaxDetail);
}

// Log flushing plus one blocking error dialog. Returns; the caller exits.
void FatalError(const char* title, const std::string& detail);

// One-shot claim shared by the abort and terminate handlers: the two fire
// for the same crash, but the user sees one dialog. Warn-and-continue
// callers do not claim, so a later fatal still shows.
bool ClaimFatal();

// Next-launch crash report. The UWP backend of FatalError leaves the text
// in this file; TakePending reads and deletes it, or returns null when the
// last run ended cleanly.
inline std::string PendingFileName() { return "fatal-pending.txt"; }

// Larger than kMaxDetail: tolerate hand-written markers, not runaway files.
constexpr size_t kMaxPending = 8192;
inline std::filesystem::path& PendingDirectory() {
  static std::filesystem::path directory;
  return directory;
}

inline void SetDirectory(std::filesystem::path directory) {
  PendingDirectory() = std::move(directory);
}

inline std::optional<std::string> TakePending() {
  std::filesystem::path& dir = PendingDirectory();
  if (dir.empty()) return std::nullopt;
  const std::filesystem::path path = dir / PendingFileName();
  std::error_code error;
  const auto size = std::filesystem::file_size(path, error);
  if (error || size == 0 || size > kMaxPending) {
    std::filesystem::remove(path, error);
    return std::nullopt;
  }
  std::ifstream in(path, std::ios::binary);
  if (!in) {
    std::filesystem::remove(path, error);
    return std::nullopt;
  }
  std::string text(size_t(size), '\0');
  in.read(text.data(), std::streamsize(size));
  text.resize(size_t(in.gcount()));
  std::filesystem::remove(path, error);
  if (text.empty()) return std::nullopt;
  return text;
}

// True when a report waits for the next launch. Startup warnings defer to
// it so a crash report is never overwritten before it is shown.
inline bool HasPending() {
  if (PendingDirectory().empty()) return false;
  std::error_code error;
  const std::filesystem::path path = PendingDirectory() / PendingFileName();
  if (!std::filesystem::is_regular_file(path, error)) return false;
  const auto size = std::filesystem::file_size(path, error);
  return !error && size > 0 && size <= kMaxPending;
}

}  // namespace fatal_dialog
