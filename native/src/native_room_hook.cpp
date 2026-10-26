#include "onevs100_init.h"
#include <rex/hook.h>
#include <rex/system/xmemory.h>
#include <rex/memory/utils.h>
#include "native_room.h"
#include "local_profile.h"
#include "native_video.h"
#include "avatar_native/native_provider.h"
#include <rex/filesystem.h>
#include <cstring>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>

// Host-only entry points in the dispatch thunk reserve [8298D438, 8299D438).
// The runtime's AllocateThunk hands out reserve slots upward from 8298D438 for
// kernel callbacks, so the old fixed slots 8298D440/8298D448 were overwritten
// at startup and Lua calls to them never reached these functions. Use the top
// of the reserve instead, far from the allocator. Mapped in main.cpp.
constexpr uint32_t kRoomThunk = 0x8299D420;
// NativeDebugLog(text) writes a Lua diagnostic line to the runtime log.
// Lua print() output never reaches the log in this build.
constexpr uint32_t kDebugLogThunk = 0x8299D428;
// NativeStorage(op, name, data): small per-player save files for the PC build
// (season score, streaks, lifetime stats), replacing the retired user storage
// service. op "read" -> contents or ""; op "write" -> "ok" / "error".
constexpr uint32_t kStorageThunk = 0x8299D430;
extern "C" REX_FUNC(__imp__sub_82135EE8);
static std::string Argument(PPCContext& ctx, uint8_t* base, uint32_t lua, int index) {
  rex::CallFrame frame(ctx);
  auto* memory = REX_KERNEL_MEMORY();
  uint32_t lengthAddress = memory->SystemHeapAlloc(4, 16);
  if (!lengthAddress) return "";
  frame.ctx.r3.u64 = lua; frame.ctx.r4.s64 = index; frame.ctx.r5.u64 = lengthAddress;
  sub_82137F68(frame.ctx, base);
  auto length = rex::memory::load_and_swap<uint32_t>(base+lengthAddress);
  memory->SystemHeapFree(lengthAddress);
  std::string value;
  if (auto address = frame.ctx.r3.u32; address && length <= 65536)
    value.assign(reinterpret_cast<const char*>(base+address), length);
  return value;
}
extern "C" REX_FUNC(native_room_call) {
  auto lua = ctx.r3.u32;
  auto operation = Argument(ctx, base, lua, 1);
  const auto value = Argument(ctx, base, lua, 2);
  // Party Avatars are served by the native Avatar provider, not the transport.
  auto result = operation == "avatar"   ? NativeAvatarManifestHex(value)
              : operation == "avatarof" ? NativeAvatarSetRemote(value)
                                        : NativeRoomOperation(operation, value);
  if (operation == "mode") REXKRNL_INFO("Native room Lua call verified mode={}", result);
  auto* memory = REX_KERNEL_MEMORY();
  uint32_t scratch = memory->SystemHeapAlloc(static_cast<uint32_t>(result.size()+1), 16);
  if (!scratch) { ctx.r3.s64 = 0; return; }
  std::memcpy(base+scratch, result.c_str(), result.size()+1);
  rex::CallFrame frame(ctx);
  frame.ctx.r3.u64 = lua; frame.ctx.r4.u64 = scratch; frame.ctx.r5.u64 = result.size();
  sub_82135D70(frame.ctx, base); // lua_pushlstring copies into Lua-owned storage.
  memory->SystemHeapFree(scratch);
  ctx.r3.s64 = 1;
}
static void PushString(PPCContext& ctx, uint8_t* base, uint32_t lua, const std::string& text) {
  auto* memory = REX_KERNEL_MEMORY();
  uint32_t scratch = memory->SystemHeapAlloc(static_cast<uint32_t>(text.size()+1), 16);
  if (!scratch) { ctx.r3.s64 = 0; return; }
  std::memcpy(base+scratch, text.c_str(), text.size()+1);
  rex::CallFrame frame(ctx);
  frame.ctx.r3.u64 = lua; frame.ctx.r4.u64 = scratch; frame.ctx.r5.u64 = text.size();
  sub_82135D70(frame.ctx, base); // lua_pushlstring copies into Lua-owned storage.
  memory->SystemHeapFree(scratch);
  ctx.r3.s64 = 1;
}
extern "C" REX_FUNC(native_storage) {
  const auto lua = ctx.r3.u32;
  const auto operation = Argument(ctx, base, lua, 1);
  const auto name = Argument(ctx, base, lua, 2);
  bool valid = !name.empty() && name.size() <= 64;
  for (char c : name)
    valid = valid && ((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '-' || c == '_');
  if (!valid) { PushString(ctx, base, lua, "error"); return; }
  const auto folder = PlayerDataFolder() / "storage";
  const auto path = folder / (name + ".dat");
  if (operation == "read") {
    std::ifstream file(path, std::ios::binary);
    std::stringstream contents;
    if (file) contents << file.rdbuf();
    auto text = contents.str();
    if (text.size() > 65536) text.clear();
    PushString(ctx, base, lua, text);
    return;
  }
  if (operation == "write") {
    const auto data = Argument(ctx, base, lua, 3);
    bool ok = false;
    try {
      std::filesystem::create_directories(folder);
      const auto temporary = folder / (name + ".tmp");
      { std::ofstream file(temporary, std::ios::binary | std::ios::trunc); file << data; file.flush(); ok = static_cast<bool>(file); }
      if (ok) std::filesystem::rename(temporary, path);
    } catch (const std::exception&) { ok = false; }
    REXKRNL_INFO("Native storage write {} bytes={} ok={}", name, data.size(), ok);
    PushString(ctx, base, lua, ok ? "ok" : "error");
    return;
  }
  PushString(ctx, base, lua, "error");
}
// "VIDEO:PLAY <name>" plays assets\media\movies\<name>.mp4 over the game
// window (intro bumper; the original .wmv is not available); "VIDEO:STOP".
static void VideoCommand(const std::string& text) {
  if (text == "VIDEO:STOP") { NativeVideoStop(); return; }
  const std::string name = text.substr(11);
  for (char c : name)
    if (!((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '_' || c == '-')) return;
  const auto file = rex::filesystem::GetExecutableFolder() / "assets" / "media" / "movies" / (name + ".mp4");
  std::error_code error;
  if (name.empty() || !std::filesystem::exists(file, error)) {
    REXKRNL_INFO("Intro video not found: {}", file.string());
    return;
  }
  REXKRNL_INFO("Intro video playing: {}", file.string());
  NativeVideoPlay(file);
}
extern "C" REX_FUNC(native_debug_log) {
  auto text = Argument(ctx, base, ctx.r3.u32, 1);
  if (text.size() > 1024) text.resize(1024);
  REXKRNL_INFO("LUA: {}", text);
  if (text == "VIDEO:STOP" || text.rfind("VIDEO:PLAY ", 0) == 0) VideoCommand(text);
  ctx.r3.s64 = 0;
}
static void RegisterGlobal(PPCContext& ctx, uint8_t* base, uint32_t lua, uint32_t thunk, const char* name) {
  rex::CallFrame frame(ctx);
  auto* memory = REX_KERNEL_MEMORY();
  const auto size = static_cast<uint32_t>(std::strlen(name) + 1);
  uint32_t scratch = memory->SystemHeapAlloc(size, 16);
  if (!scratch) return;
  std::memcpy(base+scratch, name, size);
  frame.ctx.r3.u64 = lua; frame.ctx.r4.u64 = thunk; frame.ctx.r5.s64 = 0;
  __imp__sub_82135EE8(frame.ctx, base);
  frame.ctx.r3.u64 = lua; frame.ctx.r4.s64 = -10002; frame.ctx.r5.u64 = scratch;
  sub_82136388(frame.ctx, base); // lua_setfield(L,LUA_GLOBALSINDEX,name)
  memory->SystemHeapFree(scratch);
}
static void RegisterNativeGlobals(PPCContext& ctx, uint8_t* base, uint32_t lua, const char* where) {
  RegisterGlobal(ctx, base, lua, kDebugLogThunk, "NativeDebugLog");
  RegisterGlobal(ctx, base, lua, kStorageThunk, "NativeStorage");
  REXKRNL_INFO("Native debug log Lua API registered ({}) L={:08X}", where, lua);
  if (!std::getenv("ONEVS100_ROOM_MODE")) return;
  RegisterGlobal(ctx, base, lua, kRoomThunk, "NativeRoomTransport");
  REXKRNL_INFO("Native room Lua API registered ({}) L={:08X} mode={}", where, lua,
               NativeRoomOperation("mode", ""));
}
REX_HOOK_RAW(sub_82135EE8) {
  auto lua = ctx.r3.u32;
  const bool soundRegistration = ctx.r4.u32 == 0x821A1A40;
  __imp__sub_82135EE8(ctx, base);
  // Kept for comparison: this fires once at startup, and the game scripts do
  // not see globals set here (bridge probe 2026-10-10 10:40 logged nothing).
  if (soundRegistration) RegisterNativeGlobals(ctx, base, lua, "SoundXACT");
}
// ParentalControls class registration (sub_82194570): it publishes the
// ParentalControls global with lua_setfield(L, LUA_GLOBALSINDEX, ...) on the
// state the game scripts use (they call ParentalControls() directly), so
// globals registered here on the same L are visible to them.
extern "C" REX_FUNC(__imp__sub_82194570);
REX_HOOK_RAW(sub_82194570) {
  auto lua = ctx.r3.u32;
  __imp__sub_82194570(ctx, base);
  RegisterNativeGlobals(ctx, base, lua, "ParentalControls");
}
