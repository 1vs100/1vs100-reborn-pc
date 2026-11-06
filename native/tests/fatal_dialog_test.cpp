// Truncation bound for the fatal dialog text, and the next-launch marker.
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <string>

#include "fatal_dialog.h"

int main() {
  const std::string short_text = "Game error 1234ABCD";
  if (fatal_dialog::TruncateDetail(short_text) != short_text) {
    std::printf("FAIL: short text changed\n");
    return 1;
  }
  const std::string exact(fatal_dialog::kMaxDetail, 'x');
  if (fatal_dialog::TruncateDetail(exact) != exact) {
    std::printf("FAIL: boundary text changed\n");
    return 1;
  }
  const std::string long_text(fatal_dialog::kMaxDetail + 64, 'y');
  const std::string cut = fatal_dialog::TruncateDetail(long_text);
  if (cut.size() != fatal_dialog::kMaxDetail || cut != std::string(fatal_dialog::kMaxDetail, 'y')) {
    std::printf("FAIL: long text cut to %zu\n", cut.size());
    return 1;
  }
  if (!fatal_dialog::TruncateDetail({}).empty()) {
    std::printf("FAIL: empty text changed\n");
    return 1;
  }
  const std::filesystem::path dir =
      std::filesystem::temp_directory_path() / "onevs100-fatal-marker-test";
  std::error_code error;
  std::filesystem::create_directories(dir, error);
  fatal_dialog::SetDirectory(dir);
  if (fatal_dialog::TakePending().has_value() || fatal_dialog::HasPending()) {
    std::printf("FAIL: marker present before any crash\n");
    return 1;
  }
  {
    std::ofstream out(dir / fatal_dialog::PendingFileName(), std::ios::binary);
    out << short_text;
    out.close();
    if (!fatal_dialog::HasPending()) {
      std::printf("FAIL: marker not seen while present\n");
      return 1;
    }
  }
  const auto pending = fatal_dialog::TakePending();
  if (!pending.has_value() || *pending != short_text) {
    std::printf("FAIL: marker text lost\n");
    return 1;
  }
  if (fatal_dialog::TakePending().has_value() || fatal_dialog::HasPending()) {
    std::printf("FAIL: marker read twice\n");
    return 1;
  }
  {
    std::ofstream out(dir / fatal_dialog::PendingFileName(), std::ios::binary);
  }
  if (fatal_dialog::TakePending().has_value()) {
    std::printf("FAIL: empty marker reported\n");
    return 1;
  }
  {
    std::ofstream out(dir / fatal_dialog::PendingFileName(), std::ios::binary);
    out << std::string(fatal_dialog::kMaxPending + 1, 'z');
  }
  if (fatal_dialog::TakePending().has_value()) {
    std::printf("FAIL: oversized marker reported\n");
    return 1;
  }
  if (std::filesystem::exists(dir / fatal_dialog::PendingFileName(), error) || fatal_dialog::HasPending()) {
    std::printf("FAIL: bad marker left behind\n");
    return 1;
  }
  std::filesystem::remove_all(dir, error);
  std::printf("PASS: truncation keeps %zu bytes max, marker reports once\n", fatal_dialog::kMaxDetail);
  return 0;
}
