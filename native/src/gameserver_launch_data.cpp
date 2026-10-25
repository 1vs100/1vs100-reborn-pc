// Gameserver mode: the title refuses to start (NOT_LAUNCHED_FROM_GUIDE) without
// the launch data the Xbox Guide passes, which reborn's launcher sets: type 1
// and the channel name as 32 UTF-16 characters.
#include <rex/hook.h>
#include <rex/memory/utils.h>
#include <cstring>
#include <string_view>
#include "online_service.h"
#include "runtime_export.h"

namespace {
constexpr uint32_t kLaunchDataSize = 68;
constexpr uint32_t kLaunchTypeChannel = 1;
constexpr std::string_view kChannel = "1vs100";
constexpr uint32_t kErrorSuccess = 0;
constexpr uint32_t kErrorInsufficientBuffer = 122;

bool Gameserver() { return CurrentOnlineService() == OnlineService::Gameserver; }
}  // namespace

// XamLoaderGetLaunchDataSize(size*)
REX_HOOK_RAW(__imp__XamLoaderGetLaunchDataSize) {
  static PPCFunc* const original = RuntimeExport("__imp__XamLoaderGetLaunchDataSize");
  if (!Gameserver()) { original(ctx, base); return; }
  if (ctx.r3.u32) rex::memory::store_and_swap<uint32_t>(base + ctx.r3.u32, kLaunchDataSize);
  ctx.r3.u64 = kErrorSuccess;
}

// XamLoaderGetLaunchData(buffer, size)
REX_HOOK_RAW(__imp__XamLoaderGetLaunchData) {
  static PPCFunc* const original = RuntimeExport("__imp__XamLoaderGetLaunchData");
  if (!Gameserver()) { original(ctx, base); return; }
  const uint32_t buffer = ctx.r3.u32, size = ctx.r4.u32;
  if (!buffer || size < kLaunchDataSize) { ctx.r3.u64 = kErrorInsufficientBuffer; return; }
  std::memset(base + buffer, 0, kLaunchDataSize);
  rex::memory::store_and_swap<uint32_t>(base + buffer, kLaunchTypeChannel);
  for (size_t i = 0; i < kChannel.size(); ++i)
    rex::memory::store_and_swap<uint16_t>(base + buffer + 4 + 2 * i, static_cast<uint8_t>(kChannel[i]));
  ctx.r3.u64 = kErrorSuccess;
}
