#include "update_flow.h"

#include <SDL3/SDL_log.h>

#include <expected>
#include <fstream>
#include <system_error>

#include "files.h"

namespace updater {

namespace {

constexpr const char* kCheckKey = "check_for_updates";

struct SettingsError {
  enum class Kind { Unreadable, WriteFailed } kind;
  std::filesystem::path file;
  std::string detail;
};

Notice ToNotice(const SettingsError& error) {
  const char* what = error.kind == SettingsError::Kind::Unreadable ? "Cannot read " : "Cannot write ";
  return Notice{NoticeKind::Error, what + PathText(error.file) + ": " + error.detail};
}

// Velopack's error text is Rust debug output, so it goes to the log only.
Notice ToNotice(const Error& error) {
  SDL_Log("%s: %s", Describe(error).c_str(), error.detail.c_str());
  const NoticeKind kind = error.kind == ErrorKind::CheckFailed ? NoticeKind::Background : NoticeKind::Error;
  return Notice{kind, Describe(error) + "."};
}

// Nothing when the file does not exist yet.
std::expected<std::optional<bool>, SettingsError> LoadCheckOnStart(const std::filesystem::path& file) {
  std::error_code ec;
  if (!std::filesystem::exists(file, ec)) return std::nullopt;
  auto table = ParseTomlFile(file);
  if (!table) return std::unexpected(SettingsError{SettingsError::Kind::Unreadable, file, table.error()});
  const toml::node* check = table->get(kCheckKey);
  if (!check) return std::nullopt;
  if (!check->is_boolean()) {
    return std::unexpected(
        SettingsError{SettingsError::Kind::Unreadable, file, std::string(kCheckKey) + " must be true or false"});
  }
  return check->as_boolean()->get();
}

std::expected<void, SettingsError> SaveCheckOnStart(const std::filesystem::path& file, bool on) {
  toml::table table;
  table.insert_or_assign(kCheckKey, on);
  std::ofstream out(file, std::ios::trunc);
  out << table << '\n';
  out.flush();
  if (!out) return std::unexpected(SettingsError{SettingsError::Kind::WriteFailed, file, "write error"});
  return {};
}

}  // namespace

std::optional<Notice> Flow::Open(std::filesystem::path settings_file, std::filesystem::path game) {
  settings_file_ = std::move(settings_file);
  game_ = std::move(game);
  auto stored = LoadCheckOnStart(settings_file_);
  // No check until the file is fixed, since it may say not to.
  if (!stored) return ToNotice(stored.error());
  if (*stored) check_on_start_ = **stored;
  auto manager = Manager::Open();
  if (!manager) return ToNotice(manager.error());
  manager_ = std::make_shared<Manager>(std::move(*manager));
  StartCheck();
  return std::nullopt;
}

void Flow::StartCheck() {
  if (!manager_ || !check_on_start_ || Busy()) return;
  check_.Start([manager = manager_] { return manager->Check(); });
}

void Flow::StartDownload() {
  if (!manager_ || !available_ || Busy()) return;
  progress_->percent.store(0);
  download_.Start([manager = manager_, update = available_, progress = progress_] {
    return manager->Download(*update, *progress);
  });
}

std::optional<Version> Flow::Available() const {
  if (!available_) return std::nullopt;
  return available_->version();
}

std::optional<Notice> Flow::SetCheckOnStart(bool on) {
  check_on_start_ = on;
  auto saved = SaveCheckOnStart(settings_file_, on);
  if (on && !available_) StartCheck();
  if (!saved) return ToNotice(saved.error());
  return std::nullopt;
}

PollResult Flow::Poll() {
  if (auto checked = check_.Take()) {
    if (!*checked) return PollResult{ToNotice(checked->error())};
    if (**checked) available_ = std::make_shared<const Update>(std::move(***checked));
  }
  auto downloaded = download_.Take();
  if (!downloaded) return {};
  if (!*downloaded) return PollResult{ToNotice(downloaded->error())};
  if (auto failed = Apply()) return PollResult{std::move(failed)};
  return PollResult{.exit = true};
}

std::optional<Notice> Flow::Apply() {
  if (ExecutableInUse(game_)) return Notice{NoticeKind::Error, "Close the game, then press Update again."};
  if (auto applied = manager_->ApplyAfterExit(*available_); !applied) return ToNotice(applied.error());
  return std::nullopt;
}

}  // namespace updater
