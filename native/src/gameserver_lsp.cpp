// Gameserver mode: the title's XLSP server enumerator (reborn's "LSP list")
// lists the gameserver once, as "MASSIVE", then reports no more servers. The
// runtime cannot create that enumerator, so its handle is made up here.
#include "onevs100_init.h"
#include <rex/hook.h>
#include <rex/logging.h>
#include <rex/memory/utils.h>
#include <rex/system/kernel_state.h>
#include <rex/system/xevent.h>
#include <cstring>
#include <map>
#include <mutex>
#include <optional>
#include <utility>
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

// Made-up enumerator handles stay clear of kernel object handles.
constexpr uint32_t kEnumeratorHandleBase = 0x5E000000;
constexpr uint32_t kErrorInvalidParameter = 87;
constexpr uint32_t kStatusSuccess = 0;

// XTitleServerCreateEnumerator fills the enumerator's private structure: the
// name filter at +0x18, sizes at +0xC8 and +0xCC, an XamAlloc buffer at +0xD0.
constexpr uint32_t kPrivateStructureSize = 0x100;

struct Enumerator {
  uint32_t privateStructure;
  bool served = false;
};

struct Enumerators {
  std::mutex lock;
  uint32_t next = 0;
  std::map<uint32_t, Enumerator> live;
};
Enumerators& State() {
  static Enumerators state;
  return state;
}

std::optional<uint32_t> PrivateStructure(uint32_t handle) {
  std::lock_guard guard(State().lock);
  const auto it = State().live.find(handle);
  if (it == State().live.end()) return std::nullopt;
  return it->second.privateStructure;
}
}  // namespace

// XamCreateEnumeratorHandle(user, app, open, close, extra, items, flags, out_handle)
REX_HOOK_RAW(__imp__XamCreateEnumeratorHandle) {
  static PPCFunc* const original = RuntimeExport("__imp__XamCreateEnumeratorHandle");
  const uint32_t openMessage = ctx.r5.u32, outHandle = ctx.r10.u32;
  if (CurrentOnlineService() != OnlineService::Gameserver || openMessage != kTitleServerEnumerateMessage) {
    original(ctx, base);
    return;
  }
  if (!outHandle) { ctx.r3.u64 = kErrorInvalidParameter; return; }
  const uint32_t structure = REX_KERNEL_MEMORY()->SystemHeapAlloc(kPrivateStructureSize);
  std::memset(base + structure, 0, kPrivateStructureSize);
  uint32_t handle;
  {
    std::lock_guard guard(State().lock);
    handle = kEnumeratorHandleBase | ((State().next++ & 0x00FFFFFF) << 2);
    State().live.insert_or_assign(handle, Enumerator{structure});
  }
  store_and_swap<uint32_t>(base + outHandle, handle);
  ctx.r3.u64 = kErrorSuccess;
}

// XamEnumerate(handle, flags, buffer, size, items_returned, overlapped)
REX_HOOK_RAW(__imp__XamEnumerate) {
  static PPCFunc* const original = RuntimeExport("__imp__XamEnumerate");
  const uint32_t handle = ctx.r3.u32;
  bool served;
  {
    std::lock_guard guard(State().lock);
    const auto it = State().live.find(handle);
    if (it == State().live.end()) { original(ctx, base); return; }
    served = std::exchange(it->second.served, true);
  }
  const uint32_t buffer = ctx.r5.u32, size = ctx.r6.u32, itemsReturned = ctx.r7.u32, overlapped = ctx.r8.u32;
  if (size < kServerInfoSize) { ctx.r3.u64 = kErrorInsufficientBuffer; return; }
  // As reborn: every call fills the one item; calls after the first report no more files.
  std::memset(base + buffer, 0, kServerInfoSize);
  store_and_swap<uint32_t>(base + buffer, CurrentGameserverAddress().value);
  std::memcpy(base + buffer + kServerDescription, kDescription, sizeof(kDescription));
  const uint32_t result = served ? kErrorNoMoreFiles : kErrorSuccess;
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

// NtClose(handle): the made-up enumerator handle is released here.
REX_HOOK_RAW(__imp__NtClose) {
  static PPCFunc* const original = RuntimeExport("__imp__NtClose");
  std::optional<uint32_t> structure;
  {
    std::lock_guard guard(State().lock);
    if (const auto node = State().live.extract(ctx.r3.u32)) structure = node.mapped().privateStructure;
  }
  if (!structure) { original(ctx, base); return; }
  REX_KERNEL_MEMORY()->SystemHeapFree(*structure);
  ctx.r3.u64 = kStatusSuccess;
}

// XamGetPrivateEnumStructureFromHandle(handle, out): the made-up enumerator's structure.
REX_HOOK_RAW(__imp__XamGetPrivateEnumStructureFromHandle) {
  static PPCFunc* const original = RuntimeExport("__imp__XamGetPrivateEnumStructureFromHandle");
  const uint32_t handle = ctx.r3.u32, out = ctx.r4.u32;
  const auto structure = PrivateStructure(handle);
  if (!structure) { original(ctx, base); return; }
  if (out) store_and_swap<uint32_t>(base + out, *structure);
  ctx.r3.u64 = kStatusSuccess;
}

// ObDereferenceObject(object): the made-up structure holds no reference count.
REX_HOOK_RAW(__imp__ObDereferenceObject) {
  static PPCFunc* const original = RuntimeExport("__imp__ObDereferenceObject");
  const uint32_t object = ctx.r3.u32;
  {
    std::lock_guard guard(State().lock);
    for (const auto& [handle, enumerator] : State().live)
      if (enumerator.privateStructure == object) return;
  }
  original(ctx, base);
}
