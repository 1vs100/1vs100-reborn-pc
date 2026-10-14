#include "avatar_texture_upload.h"
#include "generated/default-v4/onevs100_init.h"
#include <cstring>
#include <rex/hook.h>
#include <rex/system/xmemory.h>
#include <cstdlib>
#include <fstream>
#include <vector>
#include <atomic>

bool UploadAvatarTexture(PPCContext& ctx, uint8_t* base,
    uint32_t format, uint32_t width, uint32_t height,
    uint32_t row_bytes, uint32_t rows, uint32_t stride, uint32_t slot_bytes,
    uint32_t scratch_address, uint32_t destination_address,
    std::span<const uint8_t> source) {
  const uint64_t scratch_end = uint64_t(scratch_address) + slot_bytes;
  const uint64_t destination_end = uint64_t(destination_address) + slot_bytes;
  if (!base || !width || !height || !rows || !row_bytes || stride < row_bytes ||
      uint64_t(stride)*rows > slot_bytes ||
      uint64_t(row_bytes)*rows != source.size() ||
      !scratch_address || !destination_address || scratch_end > 0x100000000ull ||
      destination_end > 0x100000000ull ||
      (scratch_address < destination_end && destination_address < scratch_end) ||
      ctx.r1.u32 < 4096) return false;
  // XAM 81978904 reads each serialized row into the callback's linear stride.
  std::memset(base + scratch_address, 0, slot_bytes);
  for (uint32_t row = 0; row < rows; ++row)
    std::memcpy(base + scratch_address + uint64_t(row)*stride,
                source.data() + uint64_t(row)*row_bytes, row_bytes);
  const auto saved = ctx;
  ctx.r3.u64 = format; ctx.r4.u64 = width; ctx.r5.u64 = height;
  ctx.r6.u64 = 0; ctx.r7.u64 = stride; ctx.r8.u64 = slot_bytes;
  ctx.r9.u64 = scratch_address; ctx.r10.u64 = destination_address;
  // Original game callback performs the texture conversion and GPU tiling.
  sub_822B6720(ctx, base);
  const bool success = ctx.r3.u32 != 0;
  ctx = saved;
  return success;
}

void ProbeAvatarTextureUpload(PPCContext& ctx, uint8_t* base) {
  const char* path = std::getenv("ONEVS100_AVATAR_TEXTURE_PROBE");
  if (!path || !*path) return;
  static std::atomic<bool> attempted = false;
  if (attempted.exchange(true)) return;
  std::ifstream input(path, std::ios::binary | std::ios::ate);
  if (!input || input.tellg() != 32768) {
    REXKRNL_WARN("AVATAR_TEXTURE_UPLOAD probe rejected: expected 32768-byte observed BC1 payload");
    return;
  }
  std::vector<uint8_t> source(32768);
  input.seekg(0); input.read(reinterpret_cast<char*>(source.data()), source.size());
  if (!input) return;
  auto* memory = REX_KERNEL_MEMORY();
  const uint32_t scratch = memory->SystemHeapAlloc(32768, 4096);
  const uint32_t destination = memory->SystemHeapAlloc(32768, 4096);
  const uint32_t roundtrip = memory->SystemHeapAlloc(32768, 4096);
  if (scratch && destination && roundtrip) {
    std::memset(base + destination, 0xCD, 32768);
    const bool result = UploadAvatarTexture(ctx, base, 438305106, 256, 256,
        512, 64, 512, 32768, scratch, destination, source);
    size_t changed = 0;
    for (size_t i = 0; i < 32768; ++i) changed += base[destination+i] != 0xCD;
    REXKRNL_WARN("AVATAR_TEXTURE_UPLOAD probe result={} changed_bytes={} scratch={:08X} destination={:08X}",
        result, changed, scratch, destination);
    if (result) {
      std::memset(base + roundtrip, 0xCD, 32768);
      const auto saved = ctx;
      ctx.r3.u64 = 438305106; ctx.r4.u64 = 256; ctx.r5.u64 = 256;
      ctx.r6.u64 = 0; ctx.r7.u64 = 512; ctx.r8.u64 = 32768;
      ctx.r9.u64 = destination; ctx.r10.u64 = roundtrip;
      sub_822B67E8(ctx, base);
      const bool inverse_success = ctx.r3.u32 != 0;
      ctx = saved;
      size_t mismatches = 0;
      for (size_t i = 0; i < source.size(); ++i)
        mismatches += base[roundtrip+i] != source[i];
      REXKRNL_WARN("AVATAR_TEXTURE_UPLOAD inverse result={} roundtrip_mismatches={}",
          inverse_success, mismatches);
    }
  }
  if (scratch) memory->SystemHeapFree(scratch);
  if (destination) memory->SystemHeapFree(destination);
  if (roundtrip) memory->SystemHeapFree(roundtrip);
}
