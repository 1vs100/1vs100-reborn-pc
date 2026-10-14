#pragma once
#include <rex/ppc/func.h>
#include <span>

// Caller owns mapped guest buffers and a valid guest stack. Returns false for
// unsupported layouts; does not initialize the avatar runtime or its assets.
bool UploadAvatarTexture(PPCContext& ctx, uint8_t* base,
    uint32_t format, uint32_t width, uint32_t height,
    uint32_t row_bytes, uint32_t rows, uint32_t stride, uint32_t slot_bytes,
    uint32_t scratch_address, uint32_t destination_address,
    std::span<const uint8_t> source);
void ProbeAvatarTextureUpload(PPCContext& ctx, uint8_t* base);
void ProbeAvatarModel(PPCContext& ctx, uint8_t* base);
void ProbeAvatarJoints(PPCContext& ctx, uint8_t* base);
// Destination is a caller-owned, mapped, 16-byte-aligned 6928-byte workspace.
// Builds derived native joint records; no avatar renderer ownership is implied.
bool LoadAvatarJointWorkspace(PPCContext& ctx, uint8_t* base,
    uint32_t destination, std::span<const uint8_t> decoded_records);

// An owned model: all addresses remain valid until ReleaseAvatarModel is called.
// This alone does not register a scene avatar or claim renderer readiness.
struct AvatarModelAllocation {
  uint32_t cpu=0, gpu=0, header=0;
  uint32_t cpu_bytes=0, gpu_bytes=0, groups=0, textures=0;
};
bool LoadAvatarModel(PPCContext& ctx, uint8_t* base, const char* path,
                     AvatarModelAllocation& allocation);
void ReleaseAvatarModel(AvatarModelAllocation& allocation);
