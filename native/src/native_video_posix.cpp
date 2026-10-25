#include "native_video.h"
#include <rex/logging.h>

// The video overlay is implemented for Windows only.
void NativeVideoPlay(const std::filesystem::path& file) {
  REXKRNL_WARN("Video overlay unavailable on this platform: {}", file.string());
}
void NativeVideoStop() {}
