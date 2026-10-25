// Gameserver mode: an in-game presence context puts this PC back on the
// matchmaking roster (reborn's presence roster). It covers every join path,
// including those that do not create a session here.
#include "onevs100_init.h"
#include <rex/hook.h>
#include <rex/logging.h>
#include <rex/memory/utils.h>
#include "matchmaking_client.h"
#include "online_service.h"
#include "runtime_export.h"

namespace {
constexpr uint32_t kPresenceApp = 0xFB;
constexpr uint32_t kUserSetContextMessage = 0xB0006;
constexpr uint32_t kContextPresence = 0x8001;
// 0 is the lobby.
constexpr uint32_t kPresenceLobby = 0;
// Message words: user, pointer, 0, 0, context id, value.
constexpr uint32_t kContextId = 0x10, kContextValue = 0x14, kMessageSize = 0x18;
}  // namespace

// XMsgStartIORequest(app, message, overlapped, buffer, size)
REX_HOOK_RAW(__imp__XMsgStartIORequest) {
  static PPCFunc* const original = RuntimeExport("__imp__XMsgStartIORequest");
  const uint32_t buffer = ctx.r6.u32;
  if (CurrentOnlineService() == OnlineService::Gameserver && ctx.r3.u32 == kPresenceApp &&
      ctx.r4.u32 == kUserSetContextMessage && buffer && ctx.r7.u32 >= kMessageSize &&
      rex::memory::load_and_swap<uint32_t>(base + buffer + kContextId) == kContextPresence) {
    const uint32_t value = rex::memory::load_and_swap<uint32_t>(base + buffer + kContextValue);
    if (value != kPresenceLobby) {
      REXKRNL_INFO("Matchmaking: in-game presence {}, back on the roster", value);
      MatchmakingResume();
    }
  }
  original(ctx, base);
}
