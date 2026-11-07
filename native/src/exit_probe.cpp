#include "onevs100_init.h"
#include <rex/hook.h>
#include <cstring>
#include <string_view>
#include "native_controls.h"
#include "avatar_view/avatar_view.h"
#include "crash_report.h"
#include <algorithm>
#include <cstdlib>
#include <string>
#include <rex/logging/api.h>
#include <rex/platform.h>
#include "fatal_dialog.h"

// Player 1's pad state: the runtime's result (SDL/XInput) plus the optional
// native sources (USB DualSense direct reader, mouse panel; see
// native_controls.h), merged into the guest X_INPUT_STATE:
//   +0 packet number (u32), +4 buttons (u16), +6 LT, +7 RT (u8),
//   +8 LX, +10 LY, +12 RX, +14 RY (s16), all big-endian.
static void StoreU16(uint8_t* base, uint32_t address, uint16_t value) {
  REX_STORE_U8(address, static_cast<uint8_t>(value >> 8));
  REX_STORE_U8(address + 1, static_cast<uint8_t>(value & 0xFF));
}
static int16_t Stronger(int16_t a, int16_t b) { return std::abs(int(b)) > std::abs(int(a)) ? b : a; }

static uint32_t MergeNativeControls(PPCContext& ctx, uint8_t* base, uint32_t user, uint32_t output) {
  for (std::string line; NativeControlsTakeLog(line);) REXKRNL_INFO("{}", line);
  NativePadState pad;
  if (user != 0 || !output || !GetNativeControls(pad)) return ctx.r3.u32;
  const bool connected = ctx.r3.u32 == 0;
  const uint16_t buttons = static_cast<uint16_t>((connected ? REX_LOAD_U16(output + 4) : 0) | pad.buttons);
  const uint8_t lt = std::max<uint8_t>(connected ? REX_LOAD_U8(output + 6) : 0, pad.lt);
  const uint8_t rt = std::max<uint8_t>(connected ? REX_LOAD_U8(output + 7) : 0, pad.rt);
  const auto axis = [&](uint32_t offset, int16_t value) {
    return connected ? Stronger(static_cast<int16_t>(REX_LOAD_U16(output + offset)), value) : value;
  };
  const int16_t lx = axis(8, pad.lx), ly = axis(10, pad.ly), rx = axis(12, pad.rx), ry = axis(14, pad.ry);
  // The packet number must change whenever the merged state changes.
  static uint32_t lastKey = 0, serial = 0;
  const uint32_t key = (uint32_t(buttons) << 16) ^ (uint32_t(lt) << 8) ^ rt ^ (uint16_t(lx) * 31u) ^
                       (uint16_t(ly) * 131u) ^ (uint16_t(rx) * 517u) ^ (uint16_t(ry) * 1031u);
  if (key != lastKey) { lastKey = key; ++serial; }
  const uint32_t packet = (connected ? REX_LOAD_U32(output) : 0) + (serial << 16);
  REX_STORE_U32(output, packet);
  StoreU16(base, output + 4, buttons);
  REX_STORE_U8(output + 6, lt);
  REX_STORE_U8(output + 7, rt);
  StoreU16(base, output + 8, static_cast<uint16_t>(lx));
  StoreU16(base, output + 10, static_cast<uint16_t>(ly));
  StoreU16(base, output + 12, static_cast<uint16_t>(rx));
  StoreU16(base, output + 14, static_cast<uint16_t>(ry));
  ctx.r3.u64 = 0;  // ERROR_SUCCESS: player 1's pad is connected
  return 0;
}

REX_HOOK_RAW(sub_82324730) {
  const auto user = ctx.r3.u32;
  const auto output = ctx.r5.u32;
  __imp__sub_82324730(ctx, base);
  MergeNativeControls(ctx, base, user, output);
  if (ctx.r3.u32 == 0 && output) {
    const auto stick = Stronger(static_cast<int16_t>(REX_LOAD_U16(output + 8)),
                                static_cast<int16_t>(REX_LOAD_U16(output + 12)));
    const uint16_t buttons = REX_LOAD_U16(output + 4);
    // The Avatar view sees an idle pad while the crash report holds it, so no press carries over.
    const bool report = crash_report::FilterPad(user, buttons);
    if (avatar_view::FilterPad(user, report ? 0 : buttons, report ? int16_t(0) : stick) || report) {
      // The game sees idle pads while the crash report or the Avatar view has them.
      for (uint32_t offset = 4; offset < 16; ++offset) REX_STORE_U8(output + offset, 0);
    }
  }
  static thread_local uint32_t prior[4] = {0xFFFFFFFF,0xFFFFFFFF,0xFFFFFFFF,0xFFFFFFFF};
  const auto buttons = ctx.r3.u32 == 0 && output ? REX_LOAD_U16(output + 4) : 0;
  const uint32_t state = (ctx.r3.u32 << 16) | buttons;
  if (user < 4 && prior[user] != state) {
    prior[user] = state;
    REXKRNL_WARN("Input probe: user={} result={} buttons={:04X}", user, ctx.r3.u32, buttons);
  }
}

REX_HOOK_RAW(sub_82324738) {
  ctx.r5.u64 = ctx.r4.u64;
  ctx.r4.s64 = 1;
  sub_82324730(ctx, base);
}

// The Lua error that reached the title's error handler, reported by the terminal path below.
static thread_local std::string g_lua_error;

REX_HOOK_RAW(sub_82135A98) {
  const auto caller = uint32_t(ctx.lr);
  __imp__sub_82135A98(ctx, base);
  if (ctx.r3.u32 && std::strncmp(reinterpret_cast<const char*>(base + ctx.r3.u32), "OFFLINE_INPUT", 13) == 0) {
    const char* message = reinterpret_cast<const char*>(base + ctx.r3.u32);
    REXKRNL_WARN("{}", std::string_view(message, strnlen(message, 1024)));
  }
  if ((caller == 0x8215C010 || caller == 0x8215C17C) && ctx.r3.u32) {
    const char* message = reinterpret_cast<const char*>(base + ctx.r3.u32);
    g_lua_error.assign(message, strnlen(message, 1024));
    REXKRNL_ERROR("Guest Lua terminal error: {}", g_lua_error);
  }
}

// The title's terminal routine waits 6 s and then stores to guest address 0, which the host
// faults on forever. Report the error and end the process instead.
REX_HOOK_RAW(sub_82121648) {
  std::string text = fmt::format("Game error {:08X} (caller {:08X})", ctx.r4.u32, uint32_t(ctx.lr));
  if (!g_lua_error.empty()) text += ": " + g_lua_error;
  REXKRNL_ERROR("Guest terminal path: {}", text);
#if REX_PLATFORM_WINRT
  text += ". See the log in the user folder.";
#else
  text += ". See the log in logs/ beside the executable.";
#endif
  fatal_dialog::FatalError("1 vs 100", text);
  std::_Exit(1);
}
