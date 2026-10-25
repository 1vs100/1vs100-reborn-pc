// Gameserver mode: the title's XLSP server enumerator (reborn's "LSP list")
// lists the gameserver once, as "MASSIVE", then reports no more servers.
#include "onevs100_init.h"
#include <rex/hook.h>
#include <rex/logging.h>
#include <rex/memory/utils.h>
#include <rex/system/kernel_state.h>
#include <rex/system/xevent.h>
#include <atomic>
#include <cstring>
#include "online_service.h"
#include "runtime_export.h"

namespace {
using rex::memory::load_and_swap;
using rex::memory::store_and_swap;

constexpr uint32_t kErrorSuccess = 0;
constexpr uint32_t kErrorInsufficientBuffer = 122;
constexpr uint32_t kErrorNoMoreFiles = 18;
constexpr uint32_t kErrorIoPending = 997;
constexpr uint32_t kTitleServerEnumerateMessage = 0x58039;
// XOVERLAPPED: result, length, context, event, ..., extended error.
constexpr uint32_t kOverlappedResult = 0, kOverlappedLength = 4, kOverlappedContext = 8, kOverlappedEvent = 0xC,
                   kOverlappedExtendedError = 0x18;

// XTITLE_SERVER_INFO: in_addr, flags, char description[200].
constexpr uint32_t kServerInfoSize = 0xD0;
constexpr uint32_t kServerDescription = 0x08;
constexpr char kDescription[] = "MASSIVE";

struct LspEnumeration {
  std::atomic<uint32_t> handle{0};
  std::atomic<bool> served{false};
};
LspEnumeration& Enumeration() {
  static LspEnumeration enumeration;
  return enumeration;
}
}  // namespace

// XamCreateEnumeratorHandle(user, app, open, close, extra, items, flags, out_handle)
REX_HOOK_RAW(__imp__XamCreateEnumeratorHandle) {
  static PPCFunc* const original = RuntimeExport("__imp__XamCreateEnumeratorHandle");
  const uint32_t openMessage = ctx.r5.u32, outHandle = ctx.r10.u32;
  original(ctx, base);
  if (CurrentOnlineService() != OnlineService::Gameserver || openMessage != kTitleServerEnumerateMessage)
    return;
  if (ctx.r3.u32 != kErrorSuccess || !outHandle) return;
  Enumeration().handle = load_and_swap<uint32_t>(base + outHandle);
  Enumeration().served = false;
}

// XamEnumerate(handle, flags, buffer, size, items_returned, overlapped)
REX_HOOK_RAW(__imp__XamEnumerate) {
  static PPCFunc* const original = RuntimeExport("__imp__XamEnumerate");
  const uint32_t handle = ctx.r3.u32;
  if (handle == 0 || handle != Enumeration().handle) { original(ctx, base); return; }
  const uint32_t buffer = ctx.r5.u32, size = ctx.r6.u32, itemsReturned = ctx.r7.u32, overlapped = ctx.r8.u32;
  if (size < kServerInfoSize) { ctx.r3.u64 = kErrorInsufficientBuffer; return; }
  // As reborn: every call fills the one item; calls after the first report no more files.
  std::memset(base + buffer, 0, kServerInfoSize);
  store_and_swap<uint32_t>(base + buffer, CurrentGameserverAddress().value);
  std::memcpy(base + buffer + kServerDescription, kDescription, sizeof(kDescription));
  const uint32_t result = Enumeration().served.exchange(true) ? kErrorNoMoreFiles : kErrorSuccess;
  if (itemsReturned) store_and_swap<uint32_t>(base + itemsReturned, 1);
  if (overlapped) {
    store_and_swap<uint32_t>(base + overlapped + kOverlappedResult, result);
    store_and_swap<uint32_t>(base + overlapped + kOverlappedLength, 1);
    store_and_swap<uint32_t>(base + overlapped + kOverlappedContext, 0);
    store_and_swap<uint32_t>(base + overlapped + kOverlappedExtendedError, 0);
    if (const uint32_t event = load_and_swap<uint32_t>(base + overlapped + kOverlappedEvent))
      if (auto ev = REX_KERNEL_OBJECTS()->LookupObject<rex::system::XEvent>(event)) ev->Set(0, false);
    ctx.r3.u64 = kErrorIoPending;
    return;
  }
  ctx.r3.u64 = result;
}

// NtClose(handle): a reused handle value must not answer as the enumerator.
REX_HOOK_RAW(__imp__NtClose) {
  static PPCFunc* const original = RuntimeExport("__imp__NtClose");
  uint32_t handle = ctx.r3.u32;
  if (handle != 0) Enumeration().handle.compare_exchange_strong(handle, 0);
  original(ctx, base);
}
