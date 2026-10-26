// Gameserver mode: the title's service lookups answer with the gameserver.
// Sites are those of 1vs100-reborn-x360's v2.0.1149.0 profile.
#include "onevs100_init.h"
#include <rex/hook.h>
#include <rex/memory/utils.h>
#include <cstring>
#include "gameserver_redirect.h"
#include "online_service.h"
#include "runtime_export.h"

namespace {
constexpr uint32_t kTitleXlspServiceId = 0x584A07DB;
constexpr uint32_t kGridDefaultUrl = 0x82018B70;
constexpr size_t kGridDefaultUrlSize = 32;
// The gameserver's Massive ad port; the title's built-in slot port is the SBG port.
constexpr uint32_t kMassiveAdPort = 1002;

bool Gameserver() { return CurrentOnlineService() == OnlineService::Gameserver; }
// Resolver outputs hold the IPv4 address in their second word.
void StoreServerAddress(uint8_t* base, uint32_t output) {
  rex::memory::store_and_swap<uint32_t>(base + output + 4, CurrentGameserverAddress().value);
}
}  // namespace

// XlspLookup(serviceId, out): the title's LSP service resolves to the gameserver.
REX_HOOK_RAW(sub_824685C0) {
  if (!Gameserver() || ctx.r3.u32 != kTitleXlspServiceId) { __imp__sub_824685C0(ctx, base); return; }
  if (ctx.r4.u32) StoreServerAddress(base, ctx.r4.u32);
  REXKRNL_INFO("Gameserver mode: XLSP service {:08X} -> gameserver", ctx.r3.u32);
  ctx.r3.u64 = 0;
}
// SBG raw resolver (obj, out).
REX_HOOK_RAW(sub_82239988) {
  if (!Gameserver()) { __imp__sub_82239988(ctx, base); return; }
  if (ctx.r4.u32) StoreServerAddress(base, ctx.r4.u32);
  ctx.r3.u64 = 0;
}
// RegisterMasterUser's SBG resolver, same contract.
REX_HOOK_RAW(sub_8223ABB0) {
  if (!Gameserver()) { __imp__sub_8223ABB0(ctx, base); return; }
  if (ctx.r4.u32) StoreServerAddress(base, ctx.r4.u32);
  ctx.r3.u64 = 0;
}
// The title's XNetServerToInAddr wrapper (in, serviceId, out): no secure address
// translation on a LAN, the address is used as given.
REX_HOOK_RAW(sub_8246AB10) {
  if (!Gameserver()) { __imp__sub_8246AB10(ctx, base); return; }
  if (ctx.r5.u32) rex::memory::store_and_swap<uint32_t>(base + ctx.r5.u32, ctx.r3.u32);
  ctx.r3.u64 = 0;
}
// Massive NetworkManager::GetServerPortU16(request): the request slot's port,
// or -1 without a slot. Polled while connecting, so it does not log.
REX_HOOK_RAW(sub_824FB3C0) {
  __imp__sub_824FB3C0(ctx, base);
  if (Gameserver() && ctx.r3.s32 >= 0) ctx.r3.u64 = kMassiveAdPort;
}
// No security associations with plain host sockets: every XNet connect succeeds.
REX_HOOK_RAW(__imp__NetDll_XNetConnect) {
  static PPCFunc* const original = RuntimeExport("__imp__NetDll_XNetConnect");
  if (!Gameserver() && original) { original(ctx, base); return; }
  ctx.r3.u64 = 0;
}

void ApplyGameserverImagePatches(uint8_t* base) {
  if (!Gameserver()) return;
  // The Grid client's built-in URL needs Live DNS.
  const std::string url = "http://" + CurrentGameserverHost() + "/grid/v1";
  if (url.size() >= kGridDefaultUrlSize) {
    REXKRNL_ERROR("Gameserver mode: Grid URL {} longer than {} bytes; built-in URL kept", url,
                  kGridDefaultUrlSize - 1);
    return;
  }
  char slot[kGridDefaultUrlSize] = {};
  std::memcpy(slot, url.data(), url.size());
  // The image's data section is mapped read-only. Protect wants a page-aligned start.
  uint8_t* target = base + kGridDefaultUrl;
  const size_t page = rex::memory::page_size();
  auto* pageStart = reinterpret_cast<uint8_t*>(reinterpret_cast<uintptr_t>(target) & ~(page - 1));
  const size_t length = (target - pageStart) + sizeof(slot);
  rex::memory::PageAccess previous;
  if (!rex::memory::Protect(pageStart, length, rex::memory::PageAccess::kReadWrite, &previous)) {
    REXKRNL_ERROR("Gameserver mode: Grid URL slot not writable; built-in URL kept");
    return;
  }
  std::memcpy(target, slot, sizeof(slot));
  rex::memory::Protect(pageStart, length, previous);
  REXKRNL_INFO("Gameserver mode: Grid URL {}", url);
}
