#pragma once
#include <filesystem>
#include <optional>

// Makes the packages named by the content setting, if any, available
// to the title without extracting them.
void InstallConfiguredContent();

// The title's own package among the content packages, used as game:
// when no unpacked game folder is present.
std::optional<std::filesystem::path> FindGamePackage();
