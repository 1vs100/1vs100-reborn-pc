#include "avatar_texture_upload.h"
#include <rex/hook.h>
#include <rex/system/xmemory.h>
#include <rex/memory/utils.h>
#include <atomic>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <vector>
#include <cmath>

void DeriveAvatarJoints(PPCContext&, uint8_t*, uint32_t);

bool LoadAvatarJointWorkspace(PPCContext& ctx, uint8_t* base,
    uint32_t destination, std::span<const uint8_t> bytes) {
  if (!base || !destination || destination%16 ||
      uint64_t(destination)+6928>0x100000000ull || bytes.size()!=6928 ||
      ctx.r1.u32<4096) return false;
  const uint32_t count=rex::memory::load_and_swap<uint32_t>(bytes.data());
  if (count<2 || count>72) return false;
  for (uint32_t index=0;index<count;++index) {
    const uint8_t* record=bytes.data()+16+index*96;
    const uint32_t parent=rex::memory::load_and_swap<uint32_t>(record);
    if ((index==0 && parent!=255) || (index && parent>=index)) return false;
    double norm=0;
    for (uint32_t field=16;field<48;field+=4) {
      const uint32_t word=rex::memory::load_and_swap<uint32_t>(record+field);
      float value; std::memcpy(&value,&word,sizeof(value));
      if (!std::isfinite(value)) return false;
      if (field>=32) norm+=double(value)*value;
    }
    if (std::abs(norm-1.)>0.0001) return false;
  }
  auto* memory=REX_KERNEL_MEMORY();
  const uint32_t constant=memory->SystemHeapAlloc(16,16);
  if (!constant) return false;
  std::memcpy(base+destination,bytes.data(),bytes.size());
  rex::memory::store_and_swap<uint32_t>(base+destination+4,destination+16);
  // Rebuild links from validated parents, rather than trusting fixture links.
  for (uint32_t index=0;index<count;++index) {
    const uint32_t record=destination+16+index*96;
    uint32_t child=255,sibling=255;
    const uint32_t parent=rex::memory::load_and_swap<uint32_t>(base+record);
    for (uint32_t next=index+1;next<count;++next) {
      const uint32_t next_parent=rex::memory::load_and_swap<uint32_t>(base+destination+16+next*96);
      if (child==255 && next_parent==index) child=next;
      if (index && sibling==255 && next_parent==parent) sibling=next;
    }
    rex::memory::store_and_swap<uint32_t>(base+record+4,child);
    rex::memory::store_and_swap<uint32_t>(base+record+8,sibling);
  }
  for (uint32_t i=0;i<4;++i)
    rex::memory::store_and_swap<uint32_t>(base+constant+i*4,0x3f800000);
  const PPCContext saved=ctx;
  ctx.r3.u64=destination;
  DeriveAvatarJoints(ctx,base,constant);
  ctx=saved;
  memory->SystemHeapFree(constant);
  return true;
}

void ProbeAvatarJoints(PPCContext& ctx, uint8_t* base) {
  const char* input=std::getenv("ONEVS100_AVATAR_JOINT_PROBE");
  const char* output=std::getenv("ONEVS100_AVATAR_JOINT_OUTPUT");
  if (!input || !*input || !output || !*output) return;
  static std::atomic<bool> attempted=false;
  if (attempted.exchange(true)) return;
  std::ifstream source(input,std::ios::binary|std::ios::ate);
  if (!source || source.tellg()!=6928 || ctx.r1.u32<4096) return;
  std::vector<uint8_t> bytes(6928);
  source.seekg(0); source.read(reinterpret_cast<char*>(bytes.data()),bytes.size());
  if (!source) return;
  const uint32_t count=rex::memory::load_and_swap<uint32_t>(bytes.data());
  if (count<2 || count>72) return;
  for (uint32_t index=1;index<count;++index) {
    const uint32_t parent=rex::memory::load_and_swap<uint32_t>(bytes.data()+16+index*96);
    if (parent>=index) return;
  }
  auto* memory=REX_KERNEL_MEMORY();
  const uint32_t skeleton=memory->SystemHeapAlloc(6928,16);
  bool written=false;
  if (skeleton && LoadAvatarJointWorkspace(ctx,base,skeleton,bytes)) {
    std::ofstream result(output,std::ios::binary);
    result.write(reinterpret_cast<const char*>(base+skeleton),6928);
    written=bool(result);
  }
  REXKRNL_WARN("AVATAR_JOINT_PROBE derived={} joints={}",written,count);
  if (skeleton) memory->SystemHeapFree(skeleton);
}
