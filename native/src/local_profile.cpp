#include "local_profile.h"
#include "local_profile_internal.h"
#include "writable_folder.h"
#include <rex/system/kernel_state.h>
#include <rex/system/xam/user_profile.h>
#include <rex/filesystem.h>
#include <rex/platform.h>
#include <filesystem>
#include <fstream>
#include <cstdlib>
#include <cctype>
#include <cstring>
#include <optional>
#include <rex/logging.h>
#ifdef _WIN32
#include <windows.h>
#if REX_PLATFORM_WINRT
#include <thread>
#include <winrt/windows.foundation.collections.h>
#include <winrt/windows.system.h>
#else
#pragma comment(lib, "advapi32.lib")
#endif
#else
#include <unistd.h>
#endif

namespace {
std::string gamertag = "Player1";
bool NameChar(unsigned char c) {
  return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
         (c >= '0' && c <= '9') || c == ' ' || c == '_' || c == '-';
}
#if REX_PLATFORM_WINRT
// Gamertag of the signed-in Xbox account. "Gamertag" is not in KnownUserProperties;
// the documented name properties are empty on Xbox. No property carries the XUID.
std::optional<std::string> AccountGamertag() {
  std::optional<std::string> result;
  // Blocking WinRT calls are not allowed on the UI thread.
  std::thread([&] {
    try {
      using namespace winrt::Windows::System;
      const auto names = winrt::single_threaded_vector<winrt::hstring>({L"Gamertag"});
      for (const auto& user : User::FindAllAsync().get()) {
        const auto props = user.GetPropertiesAsync(names.GetView()).get();
        const auto name = props.TryLookup(L"Gamertag").try_as<winrt::hstring>();
        if (name && !name->empty()) {
          result = winrt::to_string(*name);
          return;
        }
      }
    } catch (const winrt::hresult_error& error) {
      REXKRNL_ERROR("Account gamertag lookup failed: {}", winrt::to_string(error.message()));
    }
  }).join();
  return result;
}
#endif
std::string Environment(const char* name) {
  const char* value = std::getenv(name);
  return value ? value : "";
}
}

bool ValidName(const std::string& value) {
  if (value.empty() || value.size() > 15 || value.front() == ' ' || value.back() == ' ') return false;
  for (unsigned char c : value) if (!NameChar(c)) return false;
  return true;
}
bool SaveLocalProfile(const std::string& name, const std::filesystem::path& picture) {
  if (!ValidName(name)) return false;
  std::error_code error;
  const auto profiles = PlayerDataFolder() / "profiles";
  std::filesystem::create_directories(profiles, error);
  if (error) return false;
  if (!picture.empty()) {
    const auto target = rex::filesystem::GetExecutableFolder() / "assets" / "profile-pictures" / "local.png";
    std::filesystem::create_directories(target.parent_path(), error);
    if (error) return false;
    if (!std::filesystem::exists(target, error) || !std::filesystem::equivalent(picture, target, error))
      std::filesystem::copy_file(picture, target, std::filesystem::copy_options::overwrite_existing, error);
    if (error) return false;
  }
  std::ofstream saved(profiles / "local-name.txt", std::ios::trunc);
  saved << name << '\n';
  saved.flush();
  if (!saved) return false;
  gamertag = name;
  return true;
}
bool CommandLineHas(const char* argument) {
#ifdef _WIN32
  const std::wstring wide(argument, argument + std::strlen(argument));
  return wcsstr(GetCommandLineW(), wide.c_str()) != nullptr;
#else
  std::ifstream file("/proc/self/cmdline", std::ios::binary);
  for (std::string arg; std::getline(file, arg, '\0');)
    if (arg.find(argument) != std::string::npos) return true;
  return false;
#endif
}

const std::string& LocalGamertag() { return gamertag; }
uint64_t LocalXuid() {
  static const uint64_t overrideXuid = [] {
    const char* text = std::getenv("ONEVS100_PROFILE_XUID");
    if (!text || !*text) return uint64_t(0);
    char* end = nullptr;
    const uint64_t value = std::strtoull(text, &end, 16);
    // 0x00C0000000000000 bits make the title refuse the profile.
    if (!value || (end && *end) || (value & 0x00C0000000000000ull)) return uint64_t(0);
    return value;
  }();
  if (overrideXuid) return overrideXuid;
  // Every copy of the game needs its own identity, stable across launches and
  // so saved stats follow the player. Derive
  // it from this PC and this OS user; ONEVS100_PROFILE_SLOT tells apart a
  // second test copy on the same PC.
  static const uint64_t derivedXuid = [] {
    uint32_t hash = 2166136261u;  // FNV-1a
#if REX_PLATFORM_WINRT
    // Gamertags are unique, so the same account gets the same XUID on every console.
    for (unsigned char c : "xbox|" + gamertag) { hash ^= c; hash *= 16777619u; }
#elif defined(_WIN32)
    std::wstring seed;
    for (const wchar_t* name : {L"COMPUTERNAME", L"USERNAME", L"ONEVS100_PROFILE_SLOT"}) {
      wchar_t value[256] = {};
      GetEnvironmentVariableW(name, value, 256);
      seed += L"|"; seed += value;
    }
    for (wchar_t c : seed) { hash ^= static_cast<uint32_t>(c); hash *= 16777619u; }
#else
    char host[256] = {};
    gethostname(host, sizeof(host) - 1);
    const std::string seed = std::string("|") + host + "|" + Environment("USER") + "|" + Environment("ONEVS100_PROFILE_SLOT");
    for (unsigned char c : seed) { hash ^= c; hash *= 16777619u; }
#endif
    return 0x0009000000000000ull | hash;
  }();
  return derivedXuid;
}
std::filesystem::path PlayerDataFolder() {
  // ONEVS100_PLAYER_DATA lets every copy on this PC share one profile
  // and saved stats (the launcher sets it); otherwise userdata in WritableFolder.
#ifdef _WIN32
  wchar_t value[1024] = {};
  if (GetEnvironmentVariableW(L"ONEVS100_PLAYER_DATA", value, 1024) && value[0]) return std::filesystem::path(value);
#else
  if (const std::string value = Environment("ONEVS100_PLAYER_DATA"); !value.empty()) return value;
#endif
  return WritableFolder() / "userdata";
}
void InitializeLocalProfile() {
#if REX_PLATFORM_WINRT
  if (auto name = AccountGamertag()) {
    if (ValidName(*name)) {
      gamertag = *name;
      return;
    }
    REXKRNL_ERROR("Account gamertag \"{}\" is not a valid in-game name", *name);
  }
#endif
  std::ifstream saved(PlayerDataFolder() / "profiles" / "local-name.txt");
  std::string name;
  const bool requested = CommandLineHas("--edit_profile");
  if (saved && std::getline(saved, name) && ValidName(name)) {
    gamertag = name;
    if (!requested) return;
  }
  EditLocalProfile();
}
