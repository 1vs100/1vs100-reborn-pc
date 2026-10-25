// Offline mode denies guest DNS so retired services are never contacted.
// Gameserver mode hands it to the runtime. Sockets: guest_sockets.cpp; HTTP: gameserver_xhttp.cpp.
#include <rex/hook.h>
#include <rex/system/xthread.h>
#include <rex/memory/utils.h>
#include "online_service.h"
#include "runtime_export.h"

namespace {
bool ForwardToRuntime(const char* name, PPCFunc* original, PPCContext& ctx, uint8_t* base) {
  if (CurrentOnlineService() != OnlineService::Gameserver) return false;
  if (!original) {
    REXKRNL_ERROR("Gameserver mode: runtime export {} not found", name);
    return false;
  }
  original(ctx, base);
  return true;
}
}  // namespace

#define FORWARD_IN_GAMESERVER_MODE(name)                              \
  static PPCFunc* const original = RuntimeExport(#name);              \
  if (ForwardToRuntime(#name, original, ctx, base)) return

REX_HOOK_RAW(__imp__NetDll_XNetDnsLookup) {
  FORWARD_IN_GAMESERVER_MODE(__imp__NetDll_XNetDnsLookup);
  REXKRNL_WARN("Offline restoration: denied guest DNS lookup");
  if (ctx.r6.u32) {
    rex::memory::store_and_swap<uint32_t>(base + ctx.r6.u32, 0);
  }
  ctx.r3.u64 = 10050; // WSAENETDOWN; no asynchronous request was started.
}
