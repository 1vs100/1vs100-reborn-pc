// Truncation bound for the fatal dialog text.
#include <cstdio>
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
  std::printf("PASS: truncation keeps %zu bytes max\n", fatal_dialog::kMaxDetail);
  return 0;
}
