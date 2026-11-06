#include "onboarding_host/platform.h"

#include <atomic>
#include <functional>
#include <memory>
#include <mutex>
#include <set>
#include <string>
#include <thread>
#include <utility>
#include <vector>

#include <SDL3/SDL.h>
#include <winrt/windows.devices.enumeration.h>
#include <winrt/windows.devices.portable.h>
#include <winrt/windows.foundation.collections.h>
#include <winrt/windows.storage.pickers.h>
#include <winrt/windows.system.profile.h>

#include "onboarding/files.h"
#include "onboarding_host/http_server.h"
#include "onboarding_host/importer.h"
#include "onboarding_host/lan_address.h"

namespace onboarding {
namespace {

using winrt::Windows::Storage::StorageFolder;
using winrt::Windows::Storage::Pickers::FileOpenPicker;
using winrt::Windows::Storage::Pickers::FolderPicker;
using winrt::Windows::Storage::Pickers::PickerLocationId;

// Shared by every closure in the Platform, since the view only sees functions.
struct DeviceState {
  bool is_xbox = false;
  std::mutex mutex;
  std::set<std::wstring> usb_drives;
  winrt::Windows::Devices::Enumeration::DeviceWatcher watcher{nullptr};
  std::atomic<bool> picker_open{false};
  std::atomic<bool> importing{false};
};

void CopyOnWorker(std::function<void()> work) {
  std::thread([work = std::move(work)]() mutable {
    winrt::init_apartment(winrt::apartment_type::multi_threaded);
    work();
  }).detach();
}

// Events arrive on thread pool threads.
void WatchUsbDrives(const std::shared_ptr<DeviceState>& state) {
  using winrt::Windows::Devices::Enumeration::DeviceInformation;
  using winrt::Windows::Devices::Enumeration::DeviceInformationUpdate;
  using winrt::Windows::Devices::Enumeration::DeviceWatcher;
  state->is_xbox =
      winrt::Windows::System::Profile::AnalyticsInfo::VersionInfo().DeviceFamily() == L"Windows.Xbox";
  if (!state->is_xbox) return;
  state->watcher = DeviceInformation::CreateWatcher(
      winrt::Windows::Devices::Portable::StorageDevice::GetDeviceSelector());
  state->watcher.Added([state](const DeviceWatcher&, const DeviceInformation& info) {
    std::lock_guard lock(state->mutex);
    state->usb_drives.insert(std::wstring(info.Id()));
  });
  state->watcher.Removed([state](const DeviceWatcher&, const DeviceInformationUpdate& update) {
    std::lock_guard lock(state->mutex);
    state->usb_drives.erase(std::wstring(update.Id()));
  });
  state->watcher.Start();
}

// On Xbox the pickers are only useful with a USB drive, so they are enabled
// while one is connected. On a PC they are always enabled.
bool PickersAvailable(const std::shared_ptr<DeviceState>& state) {
  if (!state->is_xbox) return true;
  std::lock_guard lock(state->mutex);
  return !state->usb_drives.empty();
}

winrt::fire_and_forget PickFolder(const std::shared_ptr<DeviceState> state, FileStore& store,
                                  Catalog& catalog) {
  if (state->picker_open || state->importing) co_return;
  state->picker_open = true;
  StorageFolder folder{nullptr};
  try {
    FolderPicker picker;
    picker.SuggestedStartLocation(PickerLocationId::ComputerFolder);
    picker.FileTypeFilter().Append(L"*");
    folder = co_await picker.PickSingleFolderAsync();
  } catch (const winrt::hresult_error& error) {
    catalog.SetError("Folder picker failed: " + winrt::to_string(error.message()));
  }
  state->picker_open = false;
  if (!folder) co_return;
  state->importing = true;
  CopyOnWorker([state, &store, &catalog, folder] {
    ImportFolder(store, catalog, folder);
    state->importing = false;
  });
}

winrt::fire_and_forget PickFiles(const std::shared_ptr<DeviceState> state, FileStore& store,
                                 Catalog& catalog) {
  if (state->picker_open || state->importing) co_return;
  state->picker_open = true;
  winrt::Windows::Foundation::Collections::IVectorView<winrt::Windows::Storage::StorageFile> files{
      nullptr};
  try {
    FileOpenPicker picker;
    picker.SuggestedStartLocation(PickerLocationId::ComputerFolder);
    picker.FileTypeFilter().Append(L"*");
    files = co_await picker.PickMultipleFilesAsync();
  } catch (const winrt::hresult_error& error) {
    catalog.SetError("File picker failed: " + winrt::to_string(error.message()));
  }
  state->picker_open = false;
  if (!files || files.Size() == 0) co_return;
  state->importing = true;
  CopyOnWorker([state, &store, &catalog, files] {
    ImportFiles(store, catalog, files);
    state->importing = false;
  });
}

}  // namespace

Platform MakeWinrtPlatform() {
  auto state = std::make_shared<DeviceState>();
  try {
    WatchUsbDrives(state);
  } catch (const winrt::hresult_error&) {
    // Without the watcher the pickers stay usable.
    state->is_xbox = false;
  }

  Platform platform;
  platform.run_worker = [](std::function<void()> work) { CopyOnWorker(std::move(work)); };
  platform.pick_folder = [state](FileStore& store, Catalog& catalog) {
    PickFolder(state, store, catalog);
  };
  platform.pick_files = [state](FileStore& store, Catalog& catalog) {
    PickFiles(state, store, catalog);
  };
  platform.pickers_available = [state] { return PickersAvailable(state); };
  platform.transfer_active = [state] {
    return state->picker_open.load() || state->importing.load();
  };
  platform.lan_urls = [] { return LanIPv4Addresses(); };
  platform.make_upload_server = [](FileStore& store,
                                   Catalog& catalog) -> std::unique_ptr<UploadServer> {
    return std::make_unique<HttpServer>(store, catalog);
  };
  platform.log = [](const std::string& line) { SDL_Log("%s", line.c_str()); };
  return platform;
}

}  // namespace onboarding
