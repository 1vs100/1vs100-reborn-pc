// Gameserver mode: Massive's ad cache, the title backing file of
// CurlOpenTitleBackingFile, is a host file in the player data folder.
#include "onevs100_init.h"
#include <rex/filesystem/devices/host_path_device.h>
#include <rex/hook.h>
#include <rex/logging.h>
#include <fmt/std.h>
#include <rex/memory/utils.h>
#include <rex/system/kernel_state.h>
#include <array>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <mutex>
#include <string_view>
#include <vector>
#include "guest_call.h"
#include "local_profile.h"
#include "online_service.h"
#include "runtime_export.h"

namespace {
using rex::memory::store_and_swap;

constexpr std::string_view kMountPath = "\\Device\\MassiveCache";
constexpr std::string_view kBackingFile = "\\Device\\MassiveCache\\backing.bin";
constexpr uint32_t kNtCreateFile = 0x828F64B4;  // NtCreateFile import thunk of title build 2.0.1149.0

constexpr uint32_t kFileReadWriteData = 0x1 | 0x2;
constexpr uint32_t kSynchronize = 0x00100000;
constexpr uint32_t kFileAttributeNormal = 0x80;
constexpr uint32_t kShareReadWrite = 3;
constexpr uint32_t kFileOpenIf = 3;
constexpr uint32_t kSynchronousNonDirectory = 0x20 | 0x40;
constexpr uint32_t kHresultFail = 0x80004005;

bool MountCacheDevice() {
  static bool mounted = [] {
    const std::filesystem::path folder = PlayerDataFolder() / "massive-cache";
    std::error_code error;
    std::filesystem::create_directories(folder, error);
    // The runtime's file system opens existing files more reliably than it creates them.
    if (!error && !std::filesystem::exists(folder / "backing.bin")) std::ofstream(folder / "backing.bin");
    auto device = std::make_unique<rex::filesystem::HostPathDevice>(kMountPath, folder, false);
    if (error || !device->Initialize() || !REX_KERNEL_FS()->RegisterDevice(std::move(device))) {
      REXKRNL_ERROR("Gameserver mode: Massive cache folder {} unavailable", folder);
      return false;
    }
    return true;
  }();
  return mounted;
}

template <typename T>
std::array<uint8_t, sizeof(T)> BigEndian(T value) {
  std::array<uint8_t, sizeof(T)> bytes{};
  store_and_swap<T>(bytes.data(), value);
  return bytes;
}
}  // namespace

// CurlOpenTitleBackingFile(handle_out, offset_out): a file handle and the u64
// offset of the title's region in it.
REX_HOOK_RAW(__imp__CurlOpenTitleBackingFile) {
  static PPCFunc* const original = RuntimeExport("__imp__CurlOpenTitleBackingFile");
  if (CurrentOnlineService() != OnlineService::Gameserver) { original(ctx, base); return; }
  const uint32_t handleOut = ctx.r3.u32, offsetOut = ctx.r4.u32;
  if (!handleOut || !offsetOut || !MountCacheDevice()) { ctx.r3.u64 = kHresultFail; return; }

  GuestCall call(ctx, base);
  std::vector<uint8_t> path(kBackingFile.begin(), kBackingFile.end());
  path.push_back(0);
  const uint32_t pathAddress = call.Scratch(path);
  // ANSI_STRING: u16 length, u16 maximum, u32 buffer.
  std::array<uint8_t, 8> name{};
  store_and_swap<uint16_t>(name.data(), static_cast<uint16_t>(kBackingFile.size()));
  store_and_swap<uint16_t>(name.data() + 2, static_cast<uint16_t>(kBackingFile.size() + 1));
  store_and_swap<uint32_t>(name.data() + 4, pathAddress);
  const uint32_t nameAddress = call.Scratch(name);
  // OBJECT_ATTRIBUTES: root directory, name, attributes.
  std::array<uint8_t, 12> attributes{};
  store_and_swap<uint32_t>(attributes.data() + 4, nameAddress);
  const uint32_t attributesAddress = call.Scratch(attributes);
  const uint32_t handleAddress = call.Scratch(BigEndian<uint32_t>(0));
  const uint32_t statusBlockAddress = call.Scratch(BigEndian<uint64_t>(0));

  const int32_t status = static_cast<int32_t>(call.Call(
      kNtCreateFile, {handleAddress, kFileReadWriteData | kSynchronize, attributesAddress, statusBlockAddress, 0,
                      kFileAttributeNormal, kShareReadWrite, kFileOpenIf, kSynchronousNonDirectory}));
  if (status < 0) {
    REXKRNL_ERROR("Gameserver mode: Massive backing file open failed, status {:08X}", static_cast<uint32_t>(status));
    ctx.r3.u64 = kHresultFail;
    return;
  }
  store_and_swap<uint32_t>(base + handleOut, rex::memory::load_and_swap<uint32_t>(base + handleAddress));
  store_and_swap<uint64_t>(base + offsetOut, 0);
  REXKRNL_INFO("Gameserver mode: Massive backing file opened");
  ctx.r3.u64 = 0;
}
