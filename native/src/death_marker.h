// Death and fault markers for builds without minidump capture. The log sink
// flushes every few seconds, so a silent process death would otherwise leave
// no trace; these use raw file calls into the user folder instead.
#pragma once

#include <filesystem>

namespace death_marker {

// Writable directory for marker files; call once paths are configured.
void SetDirectory(std::filesystem::path directory);

}  // namespace death_marker
