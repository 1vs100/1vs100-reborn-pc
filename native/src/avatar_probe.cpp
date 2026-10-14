#include "avatar_texture_upload.h"
#include "avatar_native/native_provider.h"
#include "generated/default-v4/onevs100_init.h"
#include <rex/hook.h>
#include <cstdlib>
#include <fstream>
#include <vector>
#include <cstring>
#include <rex/memory/utils.h>

namespace {
bool FixtureSizes(uint32_t& cpu, uint32_t& gpu) {
  const char* path=std::getenv("ONEVS100_AVATAR_ASSET_FIXTURE");
  const char* rig=std::getenv("ONEVS100_AVATAR_JOINT_PROBE");
  if (!path || !*path || !rig || !*rig) return false;
  std::ifstream file(path,std::ios::binary);
  uint8_t header[32];
  if (!file.read(reinterpret_cast<char*>(header),32) || std::memcmp(header,"AVF1",4)) return false;
  auto le=[&](uint32_t n){return uint32_t(header[n])|uint32_t(header[n+1])<<8|uint32_t(header[n+2])<<16|uint32_t(header[n+3])<<24;};
  if (le(4)!=608 || le(8)!=205948 || le(28)!=1) return false;
  std::ifstream joints(rig,std::ios::binary|std::ios::ate);
  if (!joints || joints.tellg()!=6928) return false;
  cpu=8064+le(4);gpu=le(8);return true;
}
}

// Trace the actual guest initialization request. Keep the runtime's failure
// result until callback and resource loading are implemented.
REX_HOOK_RAW(__imp__XamAvatarInitialize) {
  if (NativeAvatarEnabled()) { NativeAvatarInitialize(ctx,base); return; }
  REXKRNL_WARN("AVATAR_PROBE initialize coordinates={} mode={} processor={} callbacks={:08X} state={:08X} flags={:08X} caller={:08X}",
      ctx.r3.u32, ctx.r4.u32, ctx.r5.u32, ctx.r6.u32, ctx.r7.u32,
      ctx.r8.u32, uint32_t(ctx.lr));
  uint32_t cpu=0,gpu=0;
  const bool local=FixtureSizes(cpu,gpu);
  REXKRNL_WARN("AVATAR_LOCAL_PROVIDER enabled={} experimental_single_outfit=true",local);
  ctx.r3.u64 = local ? 0 : 0xFFFFFFFF;
}

// Empty runtime stubs can accidentally imply success without populating the
// outputs. Explicit failure keeps callers from consuming uninitialized data.
REX_HOOK_RAW(__imp__XamAvatarGetAssetsResultSize) {
  if (NativeAvatarEnabled()) { NativeAvatarSizes(ctx,base); return; }
  REXKRNL_WARN("AVATAR_PROBE sizes mask={:08X} cpu={:08X} gpu={:08X} caller={:08X}",
      ctx.r3.u32, ctx.r4.u32, ctx.r5.u32, uint32_t(ctx.lr));
  if (ctx.r4.u32) rex::memory::store_and_swap<uint32_t>(base + ctx.r4.u32, 0);
  if (ctx.r5.u32) rex::memory::store_and_swap<uint32_t>(base + ctx.r5.u32, 0);
  uint32_t cpu=0,gpu=0;
  if (ctx.r3.u32==4095 && ctx.r4.u32 && ctx.r5.u32 && FixtureSizes(cpu,gpu)) {
    rex::memory::store_and_swap<uint32_t>(base+ctx.r4.u32,cpu);
    rex::memory::store_and_swap<uint32_t>(base+ctx.r5.u32,gpu);
    ctx.r3.u64=0;
  } else ctx.r3.u64 = 0x80004001;
}

// Development-only provider for one recovered outfit. The caller retains CPU
// and GPU buffers; no scratch pointers survive this call. Identity entries are
// deliberately zero pending their independent decoding. Never enable by default.
REX_HOOK_RAW(__imp__XamAvatarGetAssets) {
  if (NativeAvatarEnabled()) { NativeAvatarAssets(ctx,base); return; }
  uint32_t cpu_bytes=0,gpu_bytes=0;
  const uint32_t cpu=ctx.r6.u32,gpu=ctx.r7.u32,overlap=ctx.r8.u32;
  bool ok=ctx.r4.u32==4095 && cpu && gpu && cpu%16==0 && gpu%16==0 &&
      FixtureSizes(cpu_bytes,gpu_bytes) && uint64_t(cpu)+cpu_bytes<=0x100000000ull &&
      uint64_t(gpu)+gpu_bytes<=0x100000000ull;
  AvatarModelAllocation model;
  if (ok) ok=LoadAvatarModel(ctx,base,std::getenv("ONEVS100_AVATAR_ASSET_FIXTURE"),model);
  if (ok) {
    std::ifstream file(std::getenv("ONEVS100_AVATAR_JOINT_PROBE"),std::ios::binary);
    std::vector<uint8_t> rig(6928);
    ok=bool(file.read(reinterpret_cast<char*>(rig.data()),rig.size()));
    if (ok) {
      std::memset(base+cpu,0,8064);
      ok=LoadAvatarJointWorkspace(ctx,base,cpu+32,rig);
    }
    if (ok) {
      std::memcpy(base+cpu+8064,base+model.cpu,model.cpu_bytes);
      std::memcpy(base+gpu,base+model.gpu,model.gpu_bytes);
      const uint32_t descriptor=cpu+7376;
      std::memcpy(base+descriptor,base+model.header,52);
      auto relocate=[&](uint32_t address,uint32_t source,uint32_t target) {
        uint32_t value=rex::memory::load_and_swap<uint32_t>(base+address);
        rex::memory::store_and_swap<uint32_t>(base+address,target+(value-source));
      };
      for (uint32_t field:{28u,44u,48u}) relocate(descriptor+field,model.cpu,cpu+8064);
      for (uint32_t field:{32u,36u,40u}) relocate(descriptor+field,model.gpu,gpu);
      const uint32_t groups=rex::memory::load_and_swap<uint32_t>(base+descriptor+44);
      for(uint32_t i=0;i<model.groups;++i) {
        relocate(groups+i*512+504,model.gpu,gpu);
        relocate(groups+i*512+508,model.gpu,gpu);
      }
      const uint32_t textures=rex::memory::load_and_swap<uint32_t>(base+descriptor+48);
      for(uint32_t i=0;i<model.textures;++i) relocate(textures+i*44+36,model.gpu,gpu);
      auto store=[&](uint32_t offset,uint32_t value){rex::memory::store_and_swap<uint32_t>(base+cpu+offset,value);};
      store(0,cpu+32);store(8,1);store(12,cpu+6960);store(16,cpu+7376);
    }
  }
  ReleaseAvatarModel(model);
  const uint32_t result=ok?0:0x80004001;
  if(overlap) {
    rex::memory::store_and_swap<uint32_t>(base+overlap,result);
    rex::memory::store_and_swap<uint32_t>(base+overlap+24,result);
  }
  REXKRNL_WARN("AVATAR_LOCAL_PROVIDER assets={} cpu={:08X} gpu={:08X} overlap={:08X}",ok,cpu,gpu,overlap);
  ctx.r3.u64=result;
}

// Narrow diagnostic entry: let the fixed AvatarFromFile probe reach the local
// geometry provider while the full dashboard pack/animation loader is absent.
// This does not alter the public pack readiness query or normal session avatars.
REX_HOOK_RAW(sub_822100A8) {
  const uint32_t caller=uint32_t(ctx.lr);
  uint32_t cpu=0,gpu=0;
  if (caller==0x821792EC && FixtureSizes(cpu,gpu)) {
    REXKRNL_WARN("AVATAR_LOCAL_PROVIDER fixed_constructor_gate experimental=true");
    ctx.r3.u64=1;
  } else __imp__sub_822100A8(ctx,base);
}
REX_HOOK_RAW(sub_82284FF8) {
  uint32_t cpu=0,gpu=0;
  if (FixtureSizes(cpu,gpu)) {
    // Factory readiness is scoped to this synchronous probe call. Restore its
    // previous value immediately so it cannot leak to other constructors.
    const uint32_t flag=0x82A004D5;
    const uint8_t previous=base[flag];
    base[flag]=1;
    __imp__sub_82284FF8(ctx,base);
    base[flag]=previous;
    REXKRNL_WARN("AVATAR_LOCAL_PROVIDER factory_result={:08X}",ctx.r3.u32);
  } else __imp__sub_82284FF8(ctx,base);
}

// Exact 17559 manifest body-type comparison (XAM 0x8196CA80). The import's
// empty runtime stub previously left the manifest pointer in the return register.
REX_HOOK_RAW(__imp__XamAvatarManifestGetBodyType) {
  static constexpr uint8_t types[2][16]={
    {0,0,0,2,0,0,0,1,0xc1,0xc8,0xf1,9,0xa1,0x9c,0xb2,0xe0},
    {0,0,0,2,0,1,0,2,0xc1,0xc8,0xf1,9,0xa1,0x9c,0xb2,0xe0}};
  const uint32_t manifest=ctx.r3.u32;
  uint32_t type=0;
  if(manifest && uint64_t(manifest)+304<=0x100000000ull) {
    if(!std::memcmp(base+manifest+288,types[0],16)) type=1;
    else if(!std::memcmp(base+manifest+288,types[1],16)) type=2;
  }
  REXKRNL_WARN("AVATAR_MANIFEST body_type={}",type);
  ctx.r3.u64=type;
}

REX_HOOK_RAW(sub_8212EEF8) {
  const uint32_t object=ctx.r3.u32;
  uint32_t cpu=0,gpu=0;
  const bool trace=FixtureSizes(cpu,gpu);
  if(trace) REXKRNL_WARN("AVATAR_LOCAL_PROVIDER engine_load_begin object={:08X}",object);
  __imp__sub_8212EEF8(ctx,base);
  if(trace) REXKRNL_WARN("AVATAR_LOCAL_PROVIDER engine_load_end result={:08X}",ctx.r3.u32);
}
REX_HOOK_RAW(sub_8212BF38) {
  const uint32_t object=ctx.r3.u32;
  uint32_t cpu=0,gpu=0;
  const bool trace=FixtureSizes(cpu,gpu);
  if(trace) REXKRNL_WARN("AVATAR_LOCAL_PROVIDER engine_assets_begin object={:08X}",object);
  __imp__sub_8212BF38(ctx,base);
  if(trace) REXKRNL_WARN("AVATAR_LOCAL_PROVIDER engine_assets_end result={:08X}",ctx.r3.u32);
}

REX_HOOK_RAW(sub_82286068) {
  uint32_t cpu=0,gpu=0;
  if(ctx.r3.u32==4095 && ctx.r4.u32 && ctx.r5.u32 && FixtureSizes(cpu,gpu)) {
    rex::memory::store_and_swap<uint32_t>(base+ctx.r4.u32,cpu);
    rex::memory::store_and_swap<uint32_t>(base+ctx.r5.u32,gpu);
    REXKRNL_WARN("AVATAR_LOCAL_PROVIDER wrapper_sizes cpu={} gpu={}",cpu,gpu);
    ctx.r3.u64=0;
  } else __imp__sub_82286068(ctx,base);
}
