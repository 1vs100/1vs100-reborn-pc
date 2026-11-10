#include "updater.h"

#include <string_view>

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#endif

namespace updater {

namespace {

constexpr const char* kRepositoryUrl = "https://github.com/1vs100/1vs100-reborn-pc";
// Every release so far is marked prerelease, and GitHub hides those from the
// latest release query.
constexpr bool kIncludePrereleases = true;

std::string LastError() {
  const size_t size = vpkc_get_last_error(nullptr, 0);
  if (size == 0) return {};
  // The size excludes the terminator, which Velopack writes at text[size];
  // std::string keeps room for it.
  std::string text(size, '\0');
  vpkc_get_last_error(text.data(), text.size());
  return text;
}

Error Failure(ErrorKind kind) { return Error{kind, LastError()}; }

void OnProgress(void* user_data, size_t percent) {
  static_cast<Progress*>(user_data)->percent.store(static_cast<int>(percent), std::memory_order_relaxed);
}

}  // namespace

void RunStartupHooks() {
  // A downloaded update is applied only when the player asks for it.
  vpkc_app_set_auto_apply_on_startup(false);
  vpkc_app_run(nullptr);
}

bool ExecutableInUse(const std::filesystem::path& executable) {
#ifdef _WIN32
  // A mapped image refuses write access to everyone; the file is not changed.
  HANDLE file = CreateFileW(executable.c_str(), GENERIC_WRITE, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
                            nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
  if (file == INVALID_HANDLE_VALUE) return GetLastError() == ERROR_SHARING_VIOLATION;
  CloseHandle(file);
  return false;
#else
  (void)executable;
  return false;
#endif
}

std::string Describe(const Error& error) {
  std::string_view what;
  switch (error.kind) {
    case ErrorKind::OpenFailed: what = "Cannot read this install for updates"; break;
    case ErrorKind::CheckFailed: what = "Cannot check for updates"; break;
    case ErrorKind::DownloadFailed: what = "Cannot download the update"; break;
    case ErrorKind::ApplyFailed: what = "Cannot install the update"; break;
  }
  return std::string(what);
}

std::expected<Manager, Error> Manager::Open() {
  vpkc_update_source_t* source = vpkc_new_source_github(kRepositoryUrl, nullptr, kIncludePrereleases);
  if (!source) return std::unexpected(Failure(ErrorKind::OpenFailed));
  vpkc_update_manager_t* manager = nullptr;
  const bool opened = vpkc_new_update_manager_with_source(source, nullptr, nullptr, &manager);
  // The manager keeps its own reference to the source.
  vpkc_free_source(source);
  if (!opened || !manager) return std::unexpected(Failure(ErrorKind::OpenFailed));
  return Manager(manager);
}

std::expected<std::optional<Update>, Error> Manager::Check() {
  vpkc_update_info_t* info = nullptr;
  switch (vpkc_check_for_updates(manager_.get(), &info)) {
    case UPDATE_AVAILABLE: break;
    case NO_UPDATE_AVAILABLE:
    case REMOTE_IS_EMPTY: return std::nullopt;
    case UPDATE_ERROR: return std::unexpected(Failure(ErrorKind::CheckFailed));
  }
  if (!info || !info->TargetFullRelease || !info->TargetFullRelease->Version) {
    if (info) vpkc_free_update_info(info);
    return std::unexpected(Error{ErrorKind::CheckFailed, "the release feed names no version"});
  }
  Version version{info->TargetFullRelease->Version};
  return Update(info, std::move(version));
}

std::expected<void, Error> Manager::Download(const Update& update, Progress& progress) {
  if (!vpkc_download_updates(manager_.get(), update.info_.get(), OnProgress, &progress)) {
    return std::unexpected(Failure(ErrorKind::DownloadFailed));
  }
  return {};
}

std::expected<void, Error> Manager::ApplyAfterExit(const Update& update) {
  if (!vpkc_wait_exit_then_apply_updates(manager_.get(), update.info_->TargetFullRelease, false, true, nullptr, 0)) {
    return std::unexpected(Failure(ErrorKind::ApplyFailed));
  }
  return {};
}

}  // namespace updater
