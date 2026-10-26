// Gameserver mode: QNet over the matchmaking server (reborn's NativeRemoteMembers
// and QNetRelay). Roster peers join the title's session as native remote
// members, QNet sends go through the relay, relayed payloads are posted as
// network events on the title's main thread, and remote Avatar requests are
// answered with the Avatars the matchmaking client fetched.
#include "gameserver_qnet.h"
#include <rex/hook.h>
#include <rex/logging.h>
#include <rex/memory/utils.h>
#include <rex/system/kernel_state.h>
#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <cstring>
#include <optional>
#include <string>
#include <vector>
#include "Protocol.h"
#include "avatar_native/native_provider.h"
#include "guest_call.h"
#include "matchmaking_client.h"
#include "online_service.h"

namespace {
using rex::memory::load_and_swap;
using rex::memory::store_and_swap;
using Clock = std::chrono::steady_clock;

constexpr uint32_t kTitleHeapBase = 0x40000000, kTitleHeapEnd = 0x80000000, kTitleImageEnd = 0x90000000;
// Sessions are allocated below this.
constexpr uint32_t kSessionHeapEnd = 0x50000000;

namespace Session {
constexpr uint32_t kVtable = 0x820851C0;
constexpr uint32_t kListener = 0xC;
constexpr uint32_t kState = 0x14;
constexpr uint32_t kStateLobby = 3, kStateGame = 6;
constexpr uint32_t kMaxPublic = 0xA8;
constexpr uint32_t kPublicUsed = 0xAA;
constexpr uint32_t kMemberCount = 0xB4;
constexpr uint32_t kIdTable = 0x100;
constexpr uint32_t kVtblFindMember = 15 * 4;
}  // namespace Session
namespace SessionListenerVtbl {
constexpr uint32_t kMemberAdded = 1 * 4;
constexpr uint32_t kMemberRemoved = 2 * 4;
}  // namespace SessionListenerVtbl
namespace Member {
constexpr uint32_t kTag = 0x64;
constexpr uint32_t kFlags = 0x18C;
constexpr uint32_t kFlagValid = 0x08000000;
constexpr uint32_t kFlagLocal = 0x80000000;
}  // namespace Member
namespace Title {
constexpr uint32_t kSessionMemberNew = 0x82520478;
constexpr uint32_t kSessionMemberLink = 0x82512C80;
constexpr uint32_t kSessionMemberUpdate = 0x82515310;
constexpr uint32_t kSessionRecompute = 0x82514E98;
constexpr uint32_t kSessionIdAssign = 0x8251E9C8;
constexpr uint32_t kSessionIdRemove = 0x8251EA78;
constexpr uint32_t kSessionMemberUnlink = 0x82512D30;
constexpr uint32_t kSessionChainUnlink = 0x8251FAA8;
constexpr uint32_t kSessionRelease = 0x82520540;
constexpr uint32_t kMainThreadVtable = 0x82001918;
constexpr uint32_t kPostNetEvent = 0x8215FBA0;
// The main thread's event queue is loaded through this instruction's displacement.
constexpr uint32_t kEventQueueLoad = 0x821213EC;
constexpr uint32_t kAvatarHandleData = 0x82114E38;
}  // namespace Title
// QNet send block: +8 buffer length, +0xC buffer {u8 type, u16 BE length, payload}.
constexpr uint32_t kSendBufferLength = 8, kSendBuffer = 0xC;
constexpr uint32_t kAvatarRequestXuid = 8;
constexpr uint32_t kNetEventData = 18;

// Index 0: indices 1-3 would alias a local member and drop the host on sign-in.
constexpr uint32_t kRemoteMemberIndex = 0;
constexpr uint32_t kJoinAttemptBudget = 8;
constexpr auto kJoinInterval = std::chrono::milliseconds(2000);
// A rejoining console briefly leaves the roster; the grace avoids churn.
constexpr auto kReapGrace = std::chrono::milliseconds(10000);
constexpr int kDeliveriesPerPump = 8;

bool Gameserver() { return CurrentOnlineService() == OnlineService::Gameserver; }
uint32_t Load32(uint8_t* base, uint32_t address) { return load_and_swap<uint32_t>(base + address); }
void Store32(uint8_t* base, uint32_t address, uint32_t value) { store_and_swap<uint32_t>(base + address, value); }
bool TitleHeapPointer(uint32_t p, uint32_t align) { return p >= kTitleHeapBase && p < kTitleHeapEnd && !(p & (align - 1)); }
bool TitleDataPointer(uint32_t p, uint32_t align) { return p >= kTitleHeapBase && p < kTitleImageEnd && !(p & (align - 1)); }

// Native remote members, by XUID; touched on the session thread only.
struct NativeMember {
  uint64_t xuid;
  std::optional<Clock::time_point> missingSince;
};
struct NativeMembers {
  std::vector<NativeMember> joined;
  uint32_t joinAttempts = 0;
  Clock::time_point lastJoin{};
};
NativeMembers& Members() {
  static NativeMembers members;
  return members;
}
std::atomic<bool> nativeMembersPresent{false};

bool SessionReadable(uint8_t* base, uint32_t session) {
  return TitleHeapPointer(session, 4) && session < kSessionHeapEnd && Load32(base, session) == Session::kVtable;
}

uint32_t VirtualSlot(uint8_t* base, uint32_t object, uint32_t offset) { return Load32(base, Load32(base, object) + offset); }

uint32_t FindMember(PPCContext& ctx, uint8_t* base, uint32_t session, uint64_t xuid) {
  return static_cast<uint32_t>(GuestCall(ctx, base).Call(VirtualSlot(base, session, Session::kVtblFindMember), {session, xuid}));
}

void NotifyListener(PPCContext& ctx, uint8_t* base, uint32_t session, uint32_t slot, uint32_t member) {
  if (const uint32_t listener = Load32(base, session + Session::kListener))
    GuestCall(ctx, base).Call(VirtualSlot(base, listener, slot), {listener, member});
}

// The title's remote join without XAM: create, assign an id, link, update,
// recompute, mark valid, notify.
void AddRemoteMember(PPCContext& ctx, uint8_t* base, uint32_t session, const MatchmakingRemotePeer& peer) {
  std::array<uint8_t, 16> name{};
  std::memcpy(name.data(), peer.gamertag.data(), std::min<size_t>(peer.gamertag.size(), 15));
  GuestCall create(ctx, base);
  const uint32_t member = static_cast<uint32_t>(
      create.Call(Title::kSessionMemberNew,
                  {session, peer.xuid, kRemoteMemberIndex, create.Scratch(name), 0, 0, 0, 0, 0, 0}));
  REXKRNL_INFO("QNet native join: member {:08X} for {} ({:016X})", member, peer.gamertag, peer.xuid);
  if (!member) return;
  if (static_cast<int32_t>(GuestCall(ctx, base).Call(Title::kSessionIdAssign, {session + Session::kIdTable, member, 0xFF})) < 0)
    return;
  base[session + Session::kPublicUsed] += 1;
  GuestCall(ctx, base).Call(Title::kSessionMemberLink, {session, member});
  if (static_cast<int32_t>(GuestCall(ctx, base).Call(Title::kSessionMemberUpdate, {session, member, 1})) < 0) return;
  if (static_cast<int32_t>(GuestCall(ctx, base).Call(Title::kSessionRecompute, {session})) < 0) return;
  Store32(base, member + Member::kFlags, Load32(base, member + Member::kFlags) | Member::kFlagValid);
  Store32(base, session + Session::kMemberCount, Load32(base, session + Session::kMemberCount) + 1);
  NotifyListener(ctx, base, session, SessionListenerVtbl::kMemberAdded, member);
  REXKRNL_INFO("QNet native join: {} added, {} member(s)", peer.gamertag, Load32(base, session + Session::kMemberCount));
}

// The add in reverse.
void RemoveRemoteMember(PPCContext& ctx, uint8_t* base, uint32_t session, uint64_t xuid) {
  const uint32_t member = FindMember(ctx, base, session, xuid);
  if (!member || (Load32(base, member + Member::kFlags) & Member::kFlagLocal)) return;
  NotifyListener(ctx, base, session, SessionListenerVtbl::kMemberRemoved, member);
  Store32(base, member + Member::kFlags, Load32(base, member + Member::kFlags) & ~Member::kFlagValid);
  if (const uint32_t count = Load32(base, session + Session::kMemberCount)) Store32(base, session + Session::kMemberCount, count - 1);
  if (base[session + Session::kPublicUsed]) base[session + Session::kPublicUsed] -= 1;
  GuestCall(ctx, base).Call(Title::kSessionIdRemove, {session + Session::kIdTable, member});
  GuestCall(ctx, base).Call(Title::kSessionMemberUnlink, {session, member, 1});
  GuestCall(ctx, base).Call(Title::kSessionChainUnlink, {member});
  Store32(base, member + Member::kTag, 0);
  // The title holds two references: the session's and the creation's.
  GuestCall(ctx, base).Call(Title::kSessionRelease, {member});
  GuestCall(ctx, base).Call(Title::kSessionRelease, {member});
  REXKRNL_INFO("QNet native leave: {:016X} removed, {} member(s)", xuid, Load32(base, session + Session::kMemberCount));
}

void ReapDeparted(PPCContext& ctx, uint8_t* base, uint32_t session, const std::vector<MatchmakingRemotePeer>& peers) {
  auto& joined = Members().joined;
  const auto now = Clock::now();
  for (auto it = joined.begin(); it != joined.end();) {
    const bool listed = std::any_of(peers.begin(), peers.end(), [&](const auto& p) { return p.xuid == it->xuid; });
    if (listed) {
      it->missingSince.reset();
      ++it;
      continue;
    }
    if (!it->missingSince) {
      it->missingSince = now;
      REXKRNL_INFO("QNet native member {:016X} off the roster", it->xuid);
    }
    if (now - *it->missingSince < kReapGrace) {
      ++it;
      continue;
    }
    RemoveRemoteMember(ctx, base, session, it->xuid);
    it = joined.erase(it);
  }
  nativeMembersPresent = !joined.empty();
}

void JoinRosterPeers(PPCContext& ctx, uint8_t* base, uint32_t session) {
  auto& members = Members();
  const auto now = Clock::now();
  if (now - members.lastJoin < kJoinInterval) return;
  members.lastJoin = now;
  const uint32_t state = Load32(base, session + Session::kState);
  if ((state != Session::kStateLobby && state != Session::kStateGame) || Load32(base, session + Session::kMemberCount) < 1) return;
  const auto peers = MatchmakingPeers();
  // Reap even when full: a console that left would stay as a ghost.
  if (base[session + Session::kPublicUsed] < base[session + Session::kMaxPublic]) {
    for (const auto& peer : peers) {
      if (FindMember(ctx, base, session, peer.xuid)) {
        members.joinAttempts = 0;
        continue;
      }
      if (++members.joinAttempts > kJoinAttemptBudget) {
        REXKRNL_INFO("QNet native join: attempt budget spent");
        break;
      }
      AddRemoteMember(ctx, base, session, peer);
      if (std::none_of(members.joined.begin(), members.joined.end(), [&](const auto& m) { return m.xuid == peer.xuid; }))
        members.joined.push_back({peer.xuid, std::nullopt});
      break;
    }
  }
  ReapDeparted(ctx, base, session, peers);
}

std::vector<uint8_t> DecimalString(uint64_t value) {
  const std::string text = std::to_string(value);
  std::vector<uint8_t> bytes(text.begin(), text.end());
  bytes.push_back(0);
  return bytes;
}

// A guest buffer for one thread's use, allocated on first use.
uint32_t GuestBuffer(uint32_t& address, uint32_t size) {
  if (!address) address = REX_KERNEL_MEMORY()->SystemHeapAlloc(size);
  return address;
}

void DeliverRelayedPayloads(PPCContext& ctx, uint8_t* base, uint32_t mainThread) {
  // Main thread only.
  static uint32_t data = 0;
  if (!TitleHeapPointer(mainThread, 4) || Load32(base, mainThread) != Title::kMainThreadVtable) return;
  const uint32_t queue = Load32(base, mainThread + (Load32(base, Title::kEventQueueLoad) & 0xFFFF));
  if (!TitleHeapPointer(queue, 1)) return;
  for (int delivered = 0; delivered < kDeliveriesPerPump; ++delivered) {
    const auto payload = MatchmakingRelayReceive();
    if (!payload) return;
    std::memcpy(base + GuestBuffer(data, MATCHMAKING_RELAY_MAX_BYTES), payload->data.data(), payload->data.size());
    GuestCall call(ctx, base);
    const uint32_t from = call.Scratch(DecimalString(payload->from));
    const uint32_t to = call.Scratch(DecimalString(MatchmakingSelfXuid()));
    call.Call(Title::kPostNetEvent, {queue, kNetEventData, from, to, payload->data.size(), data});
  }
}

// Relays a type-0 (player event) QNet payload. The target list holds XUIDs and
// ends with 0; an empty list is everyone.
void RelayQNetSend(uint8_t* base, uint32_t targetList, uint32_t block) {
  if (!TitleDataPointer(block, 4)) return;
  const uint32_t bufferLength = Load32(base, block + kSendBufferLength);
  const uint32_t buffer = Load32(base, block + kSendBuffer);
  if (!TitleDataPointer(buffer, 1) || bufferLength < 4 || base[buffer] != 0) return;
  const uint32_t dataLength = load_and_swap<uint16_t>(base + buffer + 1);
  if (dataLength == 0 || dataLength > MATCHMAKING_RELAY_MAX_BYTES || dataLength + 3 > bufferLength) return;
  std::vector<uint64_t> targets;
  if (TitleDataPointer(targetList, 8))
    for (uint64_t xuid; targets.size() < MATCHMAKING_RELAY_MAX_TARGETS &&
                        (xuid = load_and_swap<uint64_t>(base + targetList + 8 * targets.size())) != 0;)
      targets.push_back(xuid);
  MatchmakingRelaySend(targets, std::span(base + buffer + 3, dataLength));
}
}  // namespace

void RemoveNativeMembersBeforeLeave(PPCContext& ctx, uint8_t* base, uint32_t session) {
  if (!SessionReadable(base, session)) return;
  for (const auto& member : Members().joined) RemoveRemoteMember(ctx, base, session, member.xuid);
  Members().joined.clear();
  nativeMembersPresent = false;
}

// Session::Update(session)
REX_HOOK_RAW(sub_8251CEC8) {
  const uint32_t session = ctx.r3.u32;
  if (Gameserver() && SessionReadable(base, session)) JoinRosterPeers(ctx, base, session);
  __imp__sub_8251CEC8(ctx, base);
}

// QNet SendData(self, from, targets, block, d): without a XAM data plane the
// payload goes through the relay and the send reports done.
REX_HOOK_RAW(sub_82231358) {
  if (Gameserver() && (!MatchmakingPeers().empty() || nativeMembersPresent)) {
    RelayQNetSend(base, ctx.r5.u32, ctx.r6.u32);
    ctx.r3.u64 = 1;
    return;
  }
  __imp__sub_82231358(ctx, base);
}

// Main thread pump(self): relayed payloads are posted here, outside any Lua call.
REX_HOOK_RAW(sub_82124150) {
  if (Gameserver()) DeliverRelayedPayloads(ctx, base, ctx.r3.u32);
  __imp__sub_82124150(ctx, base);
}

// RemoteRequestAvatar(manager, from, entry): the QNet request is dropped above,
// so feed the relayed Avatar once the original marked the request sent.
REX_HOOK_RAW(sub_82118910) {
  const uint32_t manager = ctx.r3.u32, entry = ctx.r5.u32;
  // Read before the original, which marks the request sent and may reuse the entry.
  const uint64_t wanted = TitleHeapPointer(entry, 8) ? load_and_swap<uint64_t>(base + entry + kAvatarRequestXuid) : 0;
  __imp__sub_82118910(ctx, base);
  if (!Gameserver() || !wanted) return;
  // One calling thread, as on the console.
  static uint32_t blob = 0;
  if (!NativeAvatarRemoteManifest(wanted, base + GuestBuffer(blob, AVATAR_METADATA_BYTES))) {
    REXKRNL_INFO("Avatar relay: nothing yet for {:016X}", wanted);
    return;
  }
  GuestCall(ctx, base).Call(Title::kAvatarHandleData, {manager, wanted, 1, AVATAR_METADATA_BYTES, blob});
}
