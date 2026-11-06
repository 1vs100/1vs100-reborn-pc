// Blocking fatal-error dialog. Call sites log first; this flushes and shows.
#pragma once

#include <cstddef>
#include <string>

namespace fatal_dialog {

constexpr size_t kMaxDetail = 1024;

// Fault paths cannot trust long strings; bound what reaches the dialog.
inline std::string TruncateDetail(const std::string& detail) {
  if (detail.size() <= kMaxDetail) return detail;
  return detail.substr(0, kMaxDetail);
}

// Log flushing plus one blocking error dialog. Returns; the caller exits.
void FatalError(const char* title, const std::string& detail);

}  // namespace fatal_dialog
