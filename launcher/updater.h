// Self-update for copies installed by a Velopack package (Setup.exe,
// AppImage, portable app bundle). The other zip and tar.xz packages and
// development builds never update themselves.
#pragma once

#include <Velopack.h>

#include <atomic>
#include <expected>
#include <filesystem>
#include <memory>
#include <optional>
#include <string>

namespace updater {

// Velopack runs its install, uninstall and update hooks through the main
// executable and may exit or restart the process, so main calls this first.
void RunStartupHooks();

// Windows cannot replace an executable that is running, and Velopack then
// keeps the old version. Always false elsewhere, where the files of a running
// program can be replaced.
bool ExecutableInUse(const std::filesystem::path& executable);

enum class ErrorKind { OpenFailed, CheckFailed, DownloadFailed, ApplyFailed };

struct Error {
  ErrorKind kind;
  // Velopack's own error text, for the log.
  std::string detail;
};

std::string Describe(const Error& error);

// A SemVer2 release version as Velopack reports it.
struct Version {
  std::string text;
};

// A release newer than the running install.
class Update {
 public:
  const Version& version() const { return version_; }

 private:
  friend class Manager;
  struct Free {
    void operator()(vpkc_update_info_t* info) const { vpkc_free_update_info(info); }
  };
  Update(vpkc_update_info_t* info, Version version) : info_(info), version_(std::move(version)) {}
  std::unique_ptr<vpkc_update_info_t, Free> info_;
  Version version_;
};

// Written from the download thread.
struct Progress {
  // 0 to 100.
  std::atomic<int> percent{0};
};

// Calls block on the network, so they belong off the UI thread. Velopack
// keeps its last error in one global, so only one call may run at a time.
class Manager {
 public:
  // Opens the install the running executable belongs to. The feed is the
  // GitHub releases of this project.
  static std::expected<Manager, Error> Open();

  // Nothing when the running version is the newest.
  std::expected<std::optional<Update>, Error> Check();

  // Downloads into Velopack's package cache.
  std::expected<void, Error> Download(const Update& update, Progress& progress);

  // Starts Velopack's updater, which waits for this process to exit, applies
  // the update and starts the launcher again. The caller then exits.
  std::expected<void, Error> ApplyAfterExit(const Update& update);

 private:
  struct Free {
    void operator()(vpkc_update_manager_t* manager) const { vpkc_free_update_manager(manager); }
  };
  explicit Manager(vpkc_update_manager_t* manager) : manager_(manager) {}
  std::unique_ptr<vpkc_update_manager_t, Free> manager_;
};

}  // namespace updater
