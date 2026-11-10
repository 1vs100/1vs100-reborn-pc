// The launcher's update steps: one check per start, a download when the
// player asks, then the hand-over to Velopack's updater. Only Velopack
// installs open it. Elsewhere it stays closed and does nothing.
#pragma once

#include <expected>
#include <filesystem>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <thread>
#include <utility>

#include "updater.h"

namespace updater {

enum class NoticeKind {
  // Routine, such as a failed check when offline. Shown only when no other
  // message is on screen.
  Background,
  Error,
};

struct Notice {
  NoticeKind kind;
  std::string text;
};

struct PollResult {
  std::optional<Notice> notice;
  // Velopack's updater has started and the launcher must exit.
  bool exit = false;
};

class Flow {
 public:
  // settings_file is launcher.toml in the data folder. game is the game
  // executable, which must not run while an update applies.
  std::optional<Notice> Open(std::filesystem::path settings_file, std::filesystem::path game);
  bool IsOpen() const { return manager_ != nullptr; }

  // Collects finished background work. Call once per frame.
  PollResult Poll();

  bool CheckOnStart() const { return check_on_start_; }
  std::optional<Notice> SetCheckOnStart(bool on);

  void StartDownload();
  // One step runs at a time, as Velopack requires.
  bool Busy() const { return check_.Running() || download_.Running(); }
  bool Downloading() const { return download_.Running(); }
  int DownloadPercent() const { return progress_->percent.load(); }
  // The newer release found by the check.
  std::optional<Version> Available() const;

 private:
  // The result of work on its own thread. The thread is detached so a
  // stalled network call does not hold up closing the launcher.
  template <typename T>
  class Task {
   public:
    template <typename Work>
    void Start(Work work) {
      auto slot = std::make_shared<Slot>();
      slot_ = slot;
      std::thread([slot, work = std::move(work)]() mutable {
        T result = work();
        std::lock_guard lock(slot->mutex);
        slot->result = std::move(result);
      }).detach();
    }
    bool Running() const { return slot_ != nullptr; }
    // The result once the work is done; the task is then idle again.
    std::optional<T> Take() {
      if (!slot_) return std::nullopt;
      std::lock_guard lock(slot_->mutex);
      if (!slot_->result) return std::nullopt;
      std::optional<T> result = std::move(slot_->result);
      slot_.reset();
      return result;
    }

   private:
    struct Slot {
      std::mutex mutex;
      std::optional<T> result;
    };
    std::shared_ptr<Slot> slot_;
  };

  void StartCheck();
  std::optional<Notice> Apply();

  std::filesystem::path settings_file_;
  std::filesystem::path game_;
  // Default on: a Velopack install exists to stay current.
  bool check_on_start_ = true;
  // Shared with the worker threads, which may outlive a closing launcher.
  std::shared_ptr<Manager> manager_;
  std::shared_ptr<Progress> progress_ = std::make_shared<Progress>();
  std::shared_ptr<const Update> available_;
  Task<std::expected<std::optional<Update>, Error>> check_;
  Task<std::expected<void, Error>> download_;
};

}  // namespace updater
