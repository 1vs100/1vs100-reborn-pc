// Gameserver mode: QNet on a local XSession without Live (reborn's
// LivelessSession). XSessionCreate completes at once with a null handle, host
// completion sets the joined flag, and QNet game commands arm their wait before
// the call because the completion event fires inside it.
#include "onevs100_init.h"
#include <rex/hook.h>
#include <rex/logging.h>
#include <rex/memory/utils.h>
#include <atomic>
#include <cstring>
#include "online_service.h"

namespace {
using rex::memory::load_and_swap;
using rex::memory::store_and_swap;

constexpr uint32_t kErrorSuccess = 0;
constexpr uint32_t kErrorInvalidParameter = 87;
constexpr uint32_t kMaxUsers = 4;
constexpr uint32_t kCallerTitle = 1;
constexpr int32_t kQNetNotHost = -2135949243;

// XSESSION_INFO: XNKID session id, XNADDR host address, XNKEY key exchange key.
constexpr uint32_t kInfoSessionId = 0, kInfoHostAddress = 8, kInfoKey = 44, kInfoSize = 60;
constexpr uint32_t kSessionIdSize = 8, kKeySize = 16;
// XOVERLAPPED: result, length, context, ..., extended error.
constexpr uint32_t kOverlappedResult = 0, kOverlappedLength = 4, kOverlappedContext = 8,
                   kOverlappedExtendedError = 0x18;

namespace Session {
constexpr uint32_t kType = 0x1C;       // 0 is no peer network
constexpr uint32_t kXamHandle = 0x20;  // null in the liveless session
constexpr uint32_t kPeerNet = 0x190;
constexpr uint32_t kFlags = 0x2E4;
constexpr uint32_t kFlagXSessionJoined = 0x01000000;
}  // namespace Session
namespace QNetWrapper {
constexpr uint32_t kCQNet = 4;
constexpr uint32_t kField16 = 16;
}  // namespace QNetWrapper
namespace QNetCmdBlock {
constexpr uint32_t kPending = 0;
constexpr uint32_t kWaitKind = 4;  // 3 StartGame, 6 EndGame, 8 LeaveGame, 0 none
constexpr uint32_t kWaitState = 8;
constexpr uint32_t kLeaveByte = 0xC;
constexpr uint32_t kLeaveDword = 0x10;
}  // namespace QNetCmdBlock
namespace CQNetVtbl {
constexpr uint32_t kLeaveGame = 0x14;
constexpr uint32_t kStartGame = 24;
constexpr uint32_t kEndGame = 28;
constexpr uint32_t kIsHost = 44;
}  // namespace CQNetVtbl
// The leave error handler loads the command block through this instruction's displacement.
constexpr uint32_t kCmdBlockLoad = 0x8222E718;

bool Gameserver() { return CurrentOnlineService() == OnlineService::Gameserver; }

uint32_t Load32(uint8_t* base, uint32_t address) { return load_and_swap<uint32_t>(base + address); }
void Store32(uint8_t* base, uint32_t address, uint32_t value) { store_and_swap<uint32_t>(base + address, value); }

uint32_t CmdBlockOf(uint8_t* base, uint32_t wrapper) { return wrapper + (Load32(base, kCmdBlockLoad) & 0xFFFF); }

// Calls the guest function at `target` with up to four arguments; returns r3.
uint32_t CallGuest(PPCContext& parent, uint8_t* base, uint32_t target, uint32_t a0, uint32_t a1 = 0,
                   uint32_t a2 = 0, uint32_t a3 = 0) {
  rex::CallFrame frame(parent);
  frame.ctx.r3.u64 = a0;
  frame.ctx.r4.u64 = a1;
  frame.ctx.r5.u64 = a2;
  frame.ctx.r6.u64 = a3;
  frame.ctx.ctr.u64 = target;
  {
    // The macro calls through the names ctx and base.
    PPCContext& ctx = frame.ctx;
    REX_CALL_INDIRECT_FUNC(ctx.ctr.u32);
  }
  return frame.ctx.r3.u32;
}

uint32_t VirtualSlot(uint8_t* base, uint32_t object, uint32_t offset) { return Load32(base, Load32(base, object) + offset); }

void ReportLeaveError(PPCContext& ctx, uint8_t* base, uint32_t wrapper, int32_t hr) {
  rex::CallFrame frame(ctx);
  frame.ctx.r3.u64 = wrapper;
  frame.ctx.r4.s64 = hr;
  frame.ctx.r5.u64 = 0;
  frame.ctx.r6.u64 = 1;
  sub_8222E6E0(frame.ctx, base);
}

// Arms the wait before the command and undoes it when the command fails.
void RunQNetGameCommand(PPCContext& ctx, uint8_t* base, uint32_t wrapper, uint32_t slot, uint32_t armKind,
                        uint32_t armState, const char* name) {
  const uint32_t cqnet = Load32(base, wrapper + QNetWrapper::kCQNet);
  const uint32_t cmd = CmdBlockOf(base, wrapper);
  base[cmd + QNetCmdBlock::kPending] = 0;
  if (!CallGuest(ctx, base, VirtualSlot(base, cqnet, CQNetVtbl::kIsHost), cqnet)) {
    REXKRNL_INFO("QNet {} refused: not the host", name);
    ReportLeaveError(ctx, base, wrapper, kQNetNotHost);
    ctx.r3.u64 = 1;
    return;
  }
  Store32(base, cmd + QNetCmdBlock::kWaitKind, armKind);
  Store32(base, cmd + QNetCmdBlock::kWaitState, armState);
  const int32_t hr = static_cast<int32_t>(CallGuest(ctx, base, VirtualSlot(base, cqnet, slot), cqnet));
  if (hr < 0) {
    Store32(base, cmd + QNetCmdBlock::kWaitKind, 0);
    REXKRNL_INFO("QNet {} failed hr={:08X}", name, static_cast<uint32_t>(hr));
    ReportLeaveError(ctx, base, wrapper, hr);
  }
  ctx.r3.u64 = 1;
}
}  // namespace

// XSessionCreate(flags, user, public_slots, private_slots, nonce, info, overlapped, handle)
REX_HOOK_RAW(sub_82572498) {
  if (!Gameserver()) { __imp__sub_82572498(ctx, base); return; }
  const uint32_t user = ctx.r4.u32, nonce = ctx.r7.u32, info = ctx.r8.u32, overlapped = ctx.r9.u32,
                 handle = ctx.r10.u32;
  if (user >= kMaxUsers || !nonce || !info || !handle) { ctx.r3.u64 = kErrorInvalidParameter; return; }
  Store32(base, handle, 0);
  std::memset(base + info, 0, kInfoSize);
  for (uint32_t i = 0; i < kSessionIdSize; ++i) base[info + kInfoSessionId + i] = static_cast<uint8_t>(i + 1);
  for (uint32_t i = 0; i < kKeySize; ++i) base[info + kInfoKey + i] = static_cast<uint8_t>(0xA0 + i);
  {
    rex::CallFrame frame(ctx);
    frame.ctx.r3.u64 = kCallerTitle;
    frame.ctx.r4.u64 = info + kInfoHostAddress;
    __imp__NetDll_XNetGetTitleXnAddr(frame.ctx, base);
  }
  if (load_and_swap<uint64_t>(base + nonce) == 0) store_and_swap<uint64_t>(base + nonce, 1);
  // Later XNet calls fail without the key registered.
  {
    rex::CallFrame frame(ctx);
    frame.ctx.r3.u64 = kCallerTitle;
    frame.ctx.r4.u64 = info + kInfoSessionId;
    frame.ctx.r5.u64 = info + kInfoKey;
    __imp__NetDll_XNetRegisterKey(frame.ctx, base);
  }
  if (overlapped) {
    Store32(base, overlapped + kOverlappedResult, kErrorSuccess);
    Store32(base, overlapped + kOverlappedLength, 0);
    Store32(base, overlapped + kOverlappedContext, 0);
    Store32(base, overlapped + kOverlappedExtendedError, kErrorSuccess);
  }
  REXKRNL_INFO("Gameserver mode: local XSessionCreate flags={:08X} user={} public={} private={}", ctx.r3.u32,
               user, ctx.r5.u32, ctx.r6.u32);
  ctx.r3.u64 = kErrorSuccess;
}

// SessionTaskCompletionCreateHost(session, hr): a null XSession handle skips the
// join that sets the joined flag, so set it after a successful host.
REX_HOOK_RAW(sub_82516538) {
  const uint32_t session = ctx.r3.u32;
  const int32_t hr = ctx.r4.s32;
  __imp__sub_82516538(ctx, base);
  if (!Gameserver() || hr < 0) return;
  const uint32_t flags = Load32(base, session + Session::kFlags);
  if (Load32(base, session + Session::kXamHandle) == 0 && Load32(base, session + Session::kType) != 0 &&
      Load32(base, session + Session::kPeerNet) != 0 && !(flags & Session::kFlagXSessionJoined))
    Store32(base, session + Session::kFlags, flags | Session::kFlagXSessionJoined);
}

// Sends this console's user state to each remote machine. Native members have no
// machine, so the update fails; that must not end the liveless session.
REX_HOOK_RAW(sub_82514FC8) {
  const uint32_t session = ctx.r3.u32;
  __imp__sub_82514FC8(ctx, base);
  if (!Gameserver() || ctx.r3.s32 >= 0 || Load32(base, session + Session::kXamHandle) != 0) return;
  static std::atomic<int> logged{0};
  if (logged++ < 16)
    REXKRNL_INFO("Gameserver mode: remote machine update failed hr={:08X}, session kept", ctx.r3.u32);
  ctx.r3.u64 = 0;
}

// QNet leave handler(wrapper, a2, a3, a4)
REX_HOOK_RAW(sub_8222F538) {
  if (!Gameserver()) { __imp__sub_8222F538(ctx, base); return; }
  const uint32_t wrapper = ctx.r3.u32;
  const uint8_t a2 = ctx.r4.u8, a3 = ctx.r5.u8, a4 = ctx.r6.u8;
  const uint32_t cqnet = Load32(base, wrapper + QNetWrapper::kCQNet);
  const uint32_t cmd = CmdBlockOf(base, wrapper);
  Store32(base, cmd + QNetCmdBlock::kWaitState, 0);
  Store32(base, cmd + QNetCmdBlock::kWaitKind, 8);
  const int32_t result =
      static_cast<int32_t>(CallGuest(ctx, base, VirtualSlot(base, cqnet, CQNetVtbl::kLeaveGame), cqnet, a2));
  base[cmd + QNetCmdBlock::kPending] = a4;
  base[cmd + QNetCmdBlock::kLeaveByte] = 0;
  Store32(base, cmd + QNetCmdBlock::kLeaveDword, 0);
  if (a3) Store32(base, wrapper + QNetWrapper::kField16, 0);
  if (result < 0) {
    Store32(base, cmd + QNetCmdBlock::kWaitKind, 0);
    REXKRNL_INFO("QNet LeaveGame failed hr={:08X}", static_cast<uint32_t>(result));
    ReportLeaveError(ctx, base, wrapper, result);
  }
  ctx.r3.u64 = 1;
}

// QNet StartGame handler(wrapper)
REX_HOOK_RAW(sub_8222F6C0) {
  if (!Gameserver()) { __imp__sub_8222F6C0(ctx, base); return; }
  RunQNetGameCommand(ctx, base, ctx.r3.u32, CQNetVtbl::kStartGame, 3, 6, "StartGame");
}

// QNet EndGame handler(wrapper)
REX_HOOK_RAW(sub_8222F7B0) {
  if (!Gameserver()) { __imp__sub_8222F7B0(ctx, base); return; }
  RunQNetGameCommand(ctx, base, ctx.r3.u32, CQNetVtbl::kEndGame, 6, 3, "EndGame");
}
