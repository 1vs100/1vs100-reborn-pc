#include "dialogue_audio.h"
#include <windows.h>
#include <mmsystem.h>
#include <rex/filesystem.h>
#include <filesystem>
#include <fstream>
#include <cstring>

bool PlayLocalDialogue(const std::string& cue) {
  // Cue IDs cannot select arbitrary paths or invoke the system sound fallback.
  if (cue.empty() || cue.size() > 128) return false;
  for (unsigned char c : cue)
    if (!((c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') ||
          (c >= '0' && c <= '9') || c == '_' || c == '-')) return false;
  const auto path = rex::filesystem::GetExecutableFolder() / "assets" / "dialogue" / (cue + ".wav");
  std::error_code error;
  if (!std::filesystem::is_regular_file(path, error) || error) return false;
  std::ifstream input(path, std::ios::binary);
  char header[12]{};
  input.read(header, sizeof(header));
  if (!input || std::memcmp(header, "RIFF", 4) || std::memcmp(header + 8, "WAVE", 4)) return false;
  return PlaySoundW(path.c_str(), nullptr, SND_FILENAME | SND_ASYNC | SND_NODEFAULT) != FALSE;
}

void StopLocalDialogue() { PlaySoundW(nullptr, nullptr, 0); }
