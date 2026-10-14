#include "avatar_texture_upload.h"
#include "generated/default-v4/onevs100_init.h"
#include <rex/hook.h>
#include <rex/memory/utils.h>
#include <rex/system/xmemory.h>
#include <atomic>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <vector>
#include <tuple>

bool LoadAvatarModel(PPCContext& ctx, uint8_t* base, const char* path,
                     AvatarModelAllocation& allocation) {
  if (!path || !*path || allocation.cpu || allocation.gpu || allocation.header) return false;
  std::ifstream file(path, std::ios::binary | std::ios::ate);
  if (!file || file.tellg() < 84 || file.tellg() > 33554432) return false;
  std::vector<uint8_t> data(static_cast<size_t>(file.tellg()));
  file.seekg(0); file.read(reinterpret_cast<char*>(data.data()), data.size());
  if (!file || std::memcmp(data.data(), "AVF1", 4)) return false;
  auto le = [&](size_t offset) -> uint32_t {
    return uint32_t(data[offset]) | uint32_t(data[offset+1])<<8 |
        uint32_t(data[offset+2])<<16 | uint32_t(data[offset+3])<<24;
  };
  const uint32_t cpu_size=le(4), gpu_size=le(8), textures=le(12);
  const uint32_t old_cpu=le(16), old_gpu=le(20), group_offset=le(24), groups=le(28);
  if (!cpu_size || !gpu_size || cpu_size>16777216 || gpu_size>16777216 ||
      textures>4096 || uint64_t(group_offset)+uint64_t(groups)*512>cpu_size ||
      uint64_t(84)+cpu_size+gpu_size>data.size()) return false;
  auto* memory=REX_KERNEL_MEMORY();
  const uint32_t cpu=memory->SystemHeapAlloc(cpu_size,4096);
  const uint32_t gpu=memory->SystemHeapAlloc(gpu_size,4096);
  const uint32_t header=memory->SystemHeapAlloc(52,16);
  bool ok=cpu && gpu && header;
  if (ok) {
    std::memcpy(base+header,data.data()+32,52);
    std::memcpy(base+cpu,data.data()+84,cpu_size);
    std::memcpy(base+gpu,data.data()+84+cpu_size,gpu_size);
    auto relocate = [&](uint32_t address,uint32_t old_base,uint32_t new_base,uint32_t capacity) {
      const uint32_t value=rex::memory::load_and_swap<uint32_t>(base+address);
      if (value<old_base || uint64_t(value)-old_base>=capacity) { ok=false; return; }
      rex::memory::store_and_swap<uint32_t>(base+address,new_base+(value-old_base));
    };
    for (uint32_t field : {28u,44u,48u}) relocate(header+field,old_cpu,cpu,cpu_size);
    for (uint32_t field : {32u,36u,40u}) relocate(header+field,old_gpu,gpu,gpu_size);
    for (uint32_t index=0;index<groups;++index) {
      relocate(cpu+group_offset+index*512+504,old_gpu,gpu,gpu_size);
      relocate(cpu+group_offset+index*512+508,old_gpu,gpu,gpu_size);
    }
    size_t cursor=84+size_t(cpu_size)+gpu_size;
    for (uint32_t index=0;ok && index<textures;++index) {
      if (cursor+44>data.size()) { ok=false; break; }
      uint32_t fields[11];
      for (size_t i=0;i<11;++i) fields[i]=le(cursor+i*4);
      cursor+=44;
      const auto [record,offset,format,width,height,row_bytes,rows,stride,slot,flags,payload]=
          std::tuple(fields[0],fields[1],fields[2],fields[3],fields[4],fields[5],fields[6],fields[7],fields[8],fields[9],fields[10]);
      if (uint64_t(record)+44>cpu_size || uint64_t(offset)+slot>gpu_size ||
          cursor+payload>data.size() || flags>3) { ok=false; break; }
      relocate(cpu+record+36,old_gpu,gpu,gpu_size);
      if (!ok) break;
      if (flags&1) {
        ok=payload==0;
        if (ok) std::memset(base+gpu+offset,0,slot);
      } else if (flags&2) {
        const uint32_t scratch=memory->SystemHeapAlloc(slot,4096);
        ok=scratch && UploadAvatarTexture(ctx,base,format,width,height,row_bytes,rows,stride,slot,
                                         scratch,gpu+offset,{data.data()+cursor,payload});
        if (scratch) memory->SystemHeapFree(scratch);
      } else { ok=false; }
      cursor+=payload;
    }
    ok=ok && cursor==data.size();
  }
  REXKRNL_WARN("AVATAR_MODEL_PROBE loaded={} cpu_bytes={} gpu_bytes={} groups={} textures={} header={:08X}",
      ok,cpu_size,gpu_size,groups,textures,header);
  if (!ok) {
    if (header) memory->SystemHeapFree(header);
    if (cpu) memory->SystemHeapFree(cpu);
    if (gpu) memory->SystemHeapFree(gpu);
    return false;
  }
  allocation={cpu,gpu,header,cpu_size,gpu_size,groups,textures};
  return true;
}

void ReleaseAvatarModel(AvatarModelAllocation& allocation) {
  auto* memory=REX_KERNEL_MEMORY();
  if (allocation.header) memory->SystemHeapFree(allocation.header);
  if (allocation.cpu) memory->SystemHeapFree(allocation.cpu);
  if (allocation.gpu) memory->SystemHeapFree(allocation.gpu);
  allocation={};
}

void ProbeAvatarModel(PPCContext& ctx, uint8_t* base) {
  const char* path=std::getenv("ONEVS100_AVATAR_MODEL_PROBE");
  if (!path || !*path) return;
  static std::atomic<bool> attempted=false;
  if (attempted.exchange(true)) return;
  AvatarModelAllocation allocation;
  if (!LoadAvatarModel(ctx,base,path,allocation)) return;
  auto* memory=REX_KERNEL_MEMORY();
  const uint32_t header=allocation.header;
  const bool ok=true;
  // Exercise the game's completion consumer with a separately owned resource
  // root. This does not install the object into the scene or imply a GPU draw.
  const char* rig_path=std::getenv("ONEVS100_AVATAR_JOINT_PROBE");
  if (ok && rig_path && *rig_path) {
    std::ifstream rig_file(rig_path,std::ios::binary|std::ios::ate);
    if (rig_file && rig_file.tellg()==6928) {
      std::vector<uint8_t> rig(6928);
      rig_file.seekg(0); rig_file.read(reinterpret_cast<char*>(rig.data()),rig.size());
      const uint32_t arena=memory->SystemHeapAlloc(8064,16);
      const uint32_t object=memory->SystemHeapAlloc(976,16);
      bool consumed=false;
      uint32_t result=0xffffffff,error=0xffffffff;
      if (rig_file && arena && object) {
        std::memset(base+arena,0,8064);
        std::memset(base+object,0,976);
        if (LoadAvatarJointWorkspace(ctx,base,arena+32,rig)) {
          auto store=[&](uint32_t address,uint32_t value) {
            rex::memory::store_and_swap<uint32_t>(base+address,value);
          };
          store(arena,arena+32);
          store(arena+8,1);
          store(arena+12,arena+6960);
          store(arena+16,arena+7376);
          std::memcpy(base+arena+7376,base+header,52);
          store(object+28,arena);
          const PPCContext saved=ctx;
          ctx.r3.u64=object;
          sub_82284A90(ctx,base);
          result=ctx.r3.u32;
          ctx=saved;
          error=rex::memory::load_and_swap<uint32_t>(base+object+112);
          consumed=result==1 && error==0 &&
              rex::memory::load_and_swap<uint32_t>(base+object+24)==arena;
        }
      }
      REXKRNL_WARN("AVATAR_RESOURCE_PROBE consumed={} result={:08X} error={:08X}",
          consumed,result,error);
      if (object) memory->SystemHeapFree(object);
      if (arena) memory->SystemHeapFree(arena);
    }
  }
  // Diagnostic ownership ends here. Scene callers retain the allocation until
  // their avatar is destroyed, then explicitly release it through the same API.
  ReleaseAvatarModel(allocation);
}

