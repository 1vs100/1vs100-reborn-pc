// First-run gate for UWP: makes sure the game files from the embedded
// manifest are on disk (browser upload, USB pickers) before the game
// starts. Returns true when everything verified, false when the user
// quits. Skips the UI entirely when the files are already in place.
#pragma once

#include <filesystem>

namespace uwp_onboarding {

bool EnsureGameFiles(const std::filesystem::path& userRoot);

}  // namespace uwp_onboarding
