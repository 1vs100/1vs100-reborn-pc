#include <rex/hook.h>
#include <rex/memory/utils.h>
#include <rex/system/xam/user_profile.h>
#include <cstring>
#include "local_profile.h"
#include <rex/system/kernel_state.h>

REX_HOOK_RAW(__imp__XamShowFriendsUI) {
  EditLocalProfile();
  ctx.r3.u64 = 0;
}

// The game's three call sites consume the returned tier byte directly.
// The SDK's FromXUID export is an empty stub, unlike its index-based export.
// The in-game SIGN IN button opens the local profile editor instead of the
// unavailable Xbox Guide, so a profile can be created or renamed at any time.
REX_HOOK_RAW(__imp__XamShowSigninUI) {
  EditLocalProfile();
  REX_KERNEL_STATE()->BroadcastNotification(0x0000000A, 1);  // XN_SYS_SIGNINCHANGED
  REX_KERNEL_STATE()->BroadcastNotification(0x00000009, 0);  // XN_SYS_UI off
  ctx.r3.u64 = 0;
}

REX_HOOK_RAW(__imp__XamUserGetMembershipTierFromXUID) {
  const auto& profile = REX_KERNEL_STATE()->user_profile();
  const bool localProfile = profile && ctx.r3.u64 == LocalXuid();
  
  ctx.r3.u64 = localProfile ? 6 : 0;
}

// Guest compatibility state for a locally simulated service profile. This
// performs no authentication and never establishes an Xbox Live connection.
REX_HOOK_RAW(__imp__XamUserGetSigninState) {
  ctx.r3.u64 = ctx.r3.u32 == 0 ? 2 : 0;
}
REX_HOOK_RAW(__imp__XamUserGetSigninInfo) {
  const uint32_t user = ctx.r3.u32;
  const uint32_t output = ctx.r5.u32;
  if (!output) { ctx.r3.u64 = 0x80070057; return; }
  std::memset(base + output, 0, 40);
  if (user != 0) { ctx.r3.u64 = 0x80070525; return; }
  const auto& profile = REX_KERNEL_STATE()->user_profile();
  rex::memory::store_and_swap<uint64_t>(base + output, LocalXuid());
  // XUSER_SIGNIN_INFO.dwInfoFlags: XUSER_INFO_FLAG_LIVE_ENABLED (1). The
  // game's GetUserInfo (sub_82168D68) maps bit 1 to type "live" and bit 2 to
  // "guest"; with 0 it reports type "local", and PendingPlayers.AddPlayers_Task
  // then shows the "Xbox LIVE Gold Membership or Guest" sign-in dialog. This
  // is a local compatibility flag only; no Live connection is made.
  rex::memory::store_and_swap<uint32_t>(base + output + 8, 1);
  rex::memory::store_and_swap<uint32_t>(base + output + 12, 2);
  const auto& name = LocalGamertag();
  std::memcpy(base + output + 24, name.c_str(), name.size() + 1);
  
  ctx.r3.u64 = 0;
}

REX_HOOK_RAW(__imp__XamUserGetName) {
  const auto user = ctx.r3.u32;
  const auto output = ctx.r4.u32;
  const auto capacity = ctx.r5.u32;
  if (user != 0) { ctx.r3.u64 = 0x80070525; return; }
  if (!output || !capacity) { ctx.r3.u64 = 0x80070057; return; }
  const auto& name = LocalGamertag();
  const auto length = std::min<size_t>(name.size(), capacity - 1);
  std::memcpy(base + output, name.data(), length);
  base[output + length] = 0;
  ctx.r3.u64 = 0;
}

// Grants every privilege, as on a Live Gold profile.
REX_HOOK_RAW(__imp__XamUserCheckPrivilege) {
  const uint32_t user = ctx.r3.u32;
  const uint32_t privilege = ctx.r4.u32;
  const uint32_t output = ctx.r5.u32;
  if (!output || (user >= 4 && user != 0xFF)) {
    ctx.r3.u64 = 87; // ERROR_INVALID_PARAMETER
    return;
  }
  if (user != 0 && user != 0xFF) {
    ctx.r3.u64 = 1317; // ERROR_NO_SUCH_USER
    return;
  }
  (void)privilege;
  const bool localAccess = true;
  rex::memory::store_and_swap<uint32_t>(base + output, localAccess ? 1 : 0);
  
  ctx.r3.u64 = 0;
}

// XamProfileFindAccount(xuid, X_XAMACCOUNTINFO*, ...). The game's
// IsAccountParentalControlled (sub_82194510 -> sub_822355E0) reads
// cached_user_flags bit 24 from this record and treats ANY failure as a
// child account, which hid the local player from Callout leaderboards.
// Supply the local profile's account record: name, XUID and Gold tier,
// no parental-control bit, no passport or key material.
REX_HOOK_RAW(__imp__XamProfileFindAccount) {
  const uint64_t xuid = ctx.r3.u64;
  const uint32_t output = ctx.r4.u32;
  const auto& profile = REX_KERNEL_STATE()->user_profile();
  if (!output) { ctx.r3.u64 = 0x80070057; return; }
  if (!profile || xuid != LocalXuid()) {
    
    ctx.r3.u64 = 0x80070490;  // HRESULT_FROM_WIN32(ERROR_NOT_FOUND)
    return;
  }
  std::memset(base + output, 0, 0x17C);  // sizeof(X_XAMACCOUNTINFO)
  const auto& name = LocalGamertag();
  for (size_t i = 0; i < name.size() && i < 15; ++i)
    rex::memory::store_and_swap<uint16_t>(base + output + 8 + i * 2,
                                          static_cast<uint8_t>(name[i]));
  rex::memory::store_and_swap<uint64_t>(base + output + 0x28, xuid);
  // cached_user_flags: tier 6 (Gold) << 20, language 1 (English) << 25.
  // Bit 24 (parental controls enabled) stays clear.
  rex::memory::store_and_swap<uint32_t>(base + output + 0x30, (6u << 20) | (1u << 25));
  
  ctx.r3.u64 = 0;
}

// XamUserGetXUID(user, type_mask, xuid*). Same answer as the sign-in info, so a
// second isolated test client (ONEVS100_PROFILE_XUID) is a distinct player.
REX_HOOK_RAW(__imp__XamUserGetXUID) {
  const uint32_t user = ctx.r3.u32;
  const uint32_t output = ctx.r5.u32;
  if (!output) { ctx.r3.u64 = 0x80070057; return; }
  if (user != 0) { rex::memory::store_and_swap<uint64_t>(base + output, 0); ctx.r3.u64 = 0x80070525; return; }
  rex::memory::store_and_swap<uint64_t>(base + output, LocalXuid());
  ctx.r3.u64 = 0;
}
