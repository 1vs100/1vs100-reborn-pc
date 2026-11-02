#include "avatar_native/native_provider.h"
#include "onevs100_init.h"
#include <rex/hook.h>
#include <rex/memory/utils.h>
#include <cstring>

REX_HOOK_RAW(__imp__XamAvatarInitialize) {
  if (NativeAvatarEnabled()) { NativeAvatarInitialize(ctx, base); return; }
  REXKRNL_WARN("AVATAR_PROBE initialize coordinates={} mode={} caller={:08X}",
      ctx.r3.u32, ctx.r4.u32, uint32_t(ctx.lr));
  ctx.r3.u64 = 0xFFFFFFFF;
}

REX_HOOK_RAW(__imp__XamAvatarGetAssetsResultSize) {
  if (NativeAvatarEnabled()) { NativeAvatarSizes(ctx, base); return; }
  if (ctx.r4.u32) rex::memory::store_and_swap<uint32_t>(base + ctx.r4.u32, 0);
  if (ctx.r5.u32) rex::memory::store_and_swap<uint32_t>(base + ctx.r5.u32, 0);
  ctx.r3.u64 = 0x80004001;
}

REX_HOOK_RAW(__imp__XamAvatarGetAssets) {
  if (NativeAvatarEnabled()) { NativeAvatarAssets(ctx, base); return; }
  ctx.r3.u64 = 0x80004001;
}

REX_HOOK_RAW(__imp__XamAvatarManifestGetBodyType) {
  static constexpr uint8_t types[2][16] = {
    {0,0,0,2,0,0,0,1,0xc1,0xc8,0xf1,9,0xa1,0x9c,0xb2,0xe0},
    {0,0,0,2,0,1,0,2,0xc1,0xc8,0xf1,9,0xa1,0x9c,0xb2,0xe0}};
  const uint32_t manifest = ctx.r3.u32;
  uint32_t type = 0;
  if (manifest && uint64_t(manifest) + 304 <= 0x100000000ull) {
    if (!std::memcmp(base + manifest + 288, types[0], 16)) type = 1;
    else if (!std::memcmp(base + manifest + 288, types[1], 16)) type = 2;
  }
  ctx.r3.u64 = type;
}
