#include "native_provider.h"
#include "native_bridge.h"
#include "local_avatar.h"
#include "../local_profile.h"
#include "../content_install.h"
#include <rex/hook.h>
#include <rex/platform.h>
#include <rex/system/kernel_state.h>
#include <rex/system/xam/user_profile.h>
#include <array>
#include <cstdlib>
#include <map>
#include <mutex>
#include <random>
namespace xe::kernel { thread_local uint8_t* native_avatar_base=nullptr; }
// Must match the translation used by the recompiled game (REX_PHYS_HOST_OFFSET).
static_assert(xe::kernel::NativeAvatarHostOffset(0xE0000000u)==rex::memory::detail::PhysicalHostOffset(0xE0000000u));
static_assert(xe::kernel::NativeAvatarHostOffset(0xDFFFFFFFu)==rex::memory::detail::PhysicalHostOffset(0xDFFFFFFFu));
static_assert(xe::kernel::NativeAvatarHostOffset(0xFDD6E000u)==rex::memory::detail::PhysicalHostOffset(0xFDD6E000u));
using xe::kernel::NativeAvatarGuest;
namespace a=xe::kernel::xna::avatar;
namespace xa=xe::kernel::xam;
std::optional<std::filesystem::path> NativeAvatarPack() {
  static const std::optional<std::filesystem::path> pack = FindAvatarPack();
  return pack;
}
bool NativeAvatarEnabled() { return NativeAvatarPack().has_value(); }
namespace xe::kernel::xna {
avatar::Catalog* XnaAvatarCatalog() {
  const auto pack = NativeAvatarPack();
  return pack ? avatar::SharedCatalog(*pack) : nullptr;
}
}
namespace {
bool Range(uint32_t pointer,uint64_t bytes) { return pointer && uint64_t(pointer)+bytes<=0x100000000ull; }
uint32_t Complete(uint32_t overlap,uint32_t result) {
 if(overlap) {
   rex::system::kernel_state()->CompleteOverlappedImmediateEx(overlap,result?1627:0,result,0);
   REXKRNL_WARN("NATIVE_AVATAR completed overlap={:08X} result={:08X}",overlap,result);
   return 997;
 }
 return result;
}
// Remote Avatars received through the gameserver matchmaking relay.
std::mutex remoteMutex;
std::map<uint64_t,std::array<uint8_t,a::kManifestBytes>> remoteManifests;
bool RemoteManifest(uint64_t xuid,uint8_t* out) {
 std::lock_guard<std::mutex> lock(remoteMutex);
 auto found=remoteManifests.find(xuid);
 if(found==remoteManifests.end())return false;
 std::memcpy(out,found->second.data(),found->second.size());return true;
}
// Lua XUIDs are decimal strings (hex accepted too).
uint64_t ParseXuid(const std::string& text) {
 if(text.empty()||text.size()>20)return 0;
 const bool decimal=text.find_first_not_of("0123456789")==std::string::npos;
 if(!decimal&&text.find_first_not_of("0123456789abcdefABCDEF")!=std::string::npos)return 0;
 return std::strtoull(text.c_str(),nullptr,decimal?10:16);
}
bool WriteManifest(uint8_t* base,uint32_t output,uint64_t xuid,int32_t body,bool random) {
 auto* catalog=xe::kernel::xna::XnaAvatarCatalog();
 if(!catalog||!Range(output,a::kManifestBytes))return false;
 static std::mutex mutex;
 static std::mt19937 generator(0x1A5100);
 std::lock_guard<std::mutex> lock(mutex);
 std::mt19937 profile(uint32_t(xuid)^uint32_t(xuid>>32));

 a::Description description;
 auto* kernel=rex::system::kernel_state();
 const bool local=!random&&kernel&&kernel->user_profile()&&xuid==LocalXuid();
 if(!random&&!local&&xuid&&RemoteManifest(xuid,NativeAvatarGuest(base,output)))return true;
 if(!(local&&local_avatar::Load(catalog,&description)))
   description=a::RandomDescription(*catalog,random?generator:profile,body);
 const auto manifest=a::SerializeManifest(catalog,description,xuid);
 std::memcpy(NativeAvatarGuest(base,output),manifest.data(),manifest.size());return true;
}
}
std::string NativeAvatarManifestHex(const std::string& text) {
 auto* catalog=xe::kernel::xna::XnaAvatarCatalog();
 const uint64_t xuid=ParseXuid(text);
 if(!catalog||!catalog->loaded()||!xuid||xuid!=LocalXuid())return "";
 a::Description description;
 if(!local_avatar::Load(catalog,&description))description=local_avatar::Default(*catalog,xuid);
 const auto manifest=a::SerializeManifest(catalog,description,xuid);
 static const char digits[]="0123456789abcdef";
 std::string hex;hex.reserve(manifest.size()*2);
 for(uint8_t byte:manifest){hex+=digits[byte>>4];hex+=digits[byte&15];}
 return hex;
}
std::string NativeAvatarSetRemote(const std::string& value) {
 auto* catalog=xe::kernel::xna::XnaAvatarCatalog();
 if(!catalog||!catalog->loaded())return "error:no-catalog";
 const auto bar=value.find('|');
 if(bar==std::string::npos)return "error:format";
 const uint64_t xuid=ParseXuid(value.substr(0,bar));
 const std::string hex=value.substr(bar+1);
 if(!xuid||xuid==LocalXuid()||hex.size()!=a::kManifestBytes*2)return "error:format";
 std::array<uint8_t,a::kManifestBytes> bytes{};
 auto nibble=[](char c)->int{return c>='0'&&c<='9'?c-'0':c>='a'&&c<='f'?c-'a'+10:c>='A'&&c<='F'?c-'A'+10:-1;};
 for(size_t i=0;i<bytes.size();i++){
  const int high=nibble(hex[i*2]),low=nibble(hex[i*2+1]);
  if(high<0||low<0)return "error:format";
  bytes[i]=uint8_t(high<<4|low);
 }
 // Never pass network bytes to the asset builder: read them as an Avatar
 // and write a fresh manifest. Items this PC does not have (another
 // player's closet) get a similar standard item, else the console default.
 a::Description description;
 if(!a::ParseManifest(catalog,bytes.data(),bytes.size(),&description))return "error:not-a-manifest";
 const size_t similar=a::SubstituteMissingItems(*catalog,&description);
 const auto clean=a::SerializeManifest(catalog,description,xuid);
 {
  std::lock_guard<std::mutex> lock(remoteMutex);
  std::memcpy(remoteManifests[xuid].data(),clean.data(),clean.size());
 }
 REXKRNL_WARN("NATIVE_AVATAR party Avatar received for {:016X} ({} missing item(s) shown as similar standard items)",xuid,similar);
 return "ok";
}
void NativeAvatarInitialize(PPCContext& ctx,uint8_t* base) {
 xe::kernel::native_avatar_base=base;
 xa::SetAvatarCoordinateSystem(ctx.r4.u32);
 const bool ready=xe::kernel::xna::XnaAvatarCatalog()!=nullptr;
 REXKRNL_WARN("NATIVE_AVATAR initialize real_catalog={} coordinates={} caller={:08X}",ready,ctx.r4.u32,uint32_t(ctx.lr));
 ctx.r3.u64=ready?0:0xFFFFFFFF;
}
void NativeAvatarSizes(PPCContext& ctx,uint8_t* base) {
 const bool ready=xe::kernel::xna::XnaAvatarCatalog()!=nullptr;
 if(Range(ctx.r4.u32,4))rex::memory::store_and_swap<uint32_t>(NativeAvatarGuest(base,ctx.r4.u32),ready?xa::kAvatarResultBufferSize:0);
 if(Range(ctx.r5.u32,4))rex::memory::store_and_swap<uint32_t>(NativeAvatarGuest(base,ctx.r5.u32),ready?xa::kAvatarGpuBufferSize:0);
 ctx.r3.u64=ready?0:0x80004005;
}
void NativeAvatarAssets(PPCContext& ctx,uint8_t* base) {
 xe::kernel::native_avatar_base=base;
 const auto overlap=ctx.r8.u32;
 uint32_t result=0x80070057;
 if(Range(ctx.r3.u32,a::kManifestBytes)&&Range(ctx.r6.u32,xa::kAvatarResultBufferSize)&&Range(ctx.r7.u32,xa::kAvatarGpuBufferSize)) {
  result=xa::BuildAvatarAssets(NativeAvatarGuest(base,ctx.r3.u32),a::kManifestBytes,ctx.r4.u32,ctx.r6.u32,ctx.r7.u32);
 }
 REXKRNL_WARN("NATIVE_AVATAR assets caller={:08X} manifest={:08X} cpu={:08X} gpu={:08X} gpu_host_offset={:X} result={:08X}",uint32_t(ctx.lr),ctx.r3.u32,ctx.r6.u32,ctx.r7.u32,xe::kernel::NativeAvatarHostOffset(ctx.r7.u32),result);
 ctx.r3.u64=Complete(overlap,result);
}
bool NativeAvatarRemoteManifest(uint64_t xuid,uint8_t* out) { return RemoteManifest(xuid,out); }
REX_HOOK_RAW(__imp__XamAvatarGetMetadataRandom) {
 if(!NativeAvatarEnabled())return; // Preserve the original empty runtime stub.
 const uint32_t count=ctx.r4.u32,out=ctx.r5.u32,overlap=ctx.r6.u32;
 const int32_t body=ctx.r3.u32==1?1:ctx.r3.u32==2?0:-1;
 bool ok=count<=100&&Range(out,uint64_t(count)*a::kManifestBytes);
 for(uint32_t i=0;ok&&i<count;i++)ok=WriteManifest(base,out+i*a::kManifestBytes,0,body,true);
 REXKRNL_WARN("NATIVE_AVATAR random count={} ready={} caller={:08X}",count,ok,uint32_t(ctx.lr));
 ctx.r3.u64=Complete(overlap,ok?0:0x80070057);
}
REX_HOOK_RAW(__imp__XamAvatarGetManifestLocalUser) {
 if(!NativeAvatarEnabled())return;
 const uint32_t user=ctx.r3.u32,output=ctx.r4.u32,overlap=ctx.r5.u32;
 // Local development identity only; no production lookup/authentication.
 const auto& profile=REX_KERNEL_STATE()->user_profile();
 const bool ok=user==0&&profile&&WriteManifest(base,output,LocalXuid(),-1,false);
 ctx.r3.u64=Complete(overlap,ok?0:0x80004005);
}
REX_HOOK_RAW(__imp__XamAvatarGetManifestsByXuid) {
 if(!NativeAvatarEnabled())return;
 const uint32_t count=ctx.r4.u32,ids=ctx.r5.u32,out=ctx.r7.u32,overlap=ctx.r8.u32;
 bool ok=NativeAvatarEnabled()&&count<=100&&Range(ids,uint64_t(count)*8)&&Range(out,uint64_t(count)*a::kManifestBytes);
 for(uint32_t i=0;ok&&i<count;i++)ok=WriteManifest(base,out+i*a::kManifestBytes,rex::memory::load_and_swap<uint64_t>(NativeAvatarGuest(base,ids+i*8)),-1,false);
 ctx.r3.u64=Complete(overlap,ok?0:0x80070057);
}
REX_HOOK_RAW(__imp__XamAvatarLoadAnimation) {
 if(!NativeAvatarEnabled())return;
 xe::kernel::native_avatar_base=base;
 const uint32_t id=ctx.r3.u32,object=ctx.r5.u32,overlap=ctx.r6.u32;
 uint32_t result=0x80070057;
 if(NativeAvatarEnabled()&&Range(id,16)&&Range(object,a::kAnimationObjectBytes)) {
  std::array<uint8_t,16> bytes;std::memcpy(bytes.data(),NativeAvatarGuest(base,id),16);
  result=xa::LoadAvatarAnimation(bytes,object,"native-game");
 }
 ctx.r3.u64=Complete(overlap,result);
}
REX_HOOK_RAW(__imp__XamAvatarGenerateMipMaps) {
 if(!NativeAvatarEnabled())return;
 xe::kernel::native_avatar_base=base;
 // Console/Nexia signature: assets, flags, size, buffer, overlap.
 const uint32_t assets=ctx.r3.u32,size=ctx.r5.u32,buffer=ctx.r6.u32,overlap=ctx.r7.u32;
 const uint32_t result=NativeAvatarEnabled()&&Range(assets,20)&&Range(buffer,size)?xa::GenerateAvatarMipMaps(assets,buffer,size):0x80070057;
 ctx.r3.u64=Complete(overlap,result);
}
REX_HOOK_RAW(__imp__XamAvatarGetInstrumentation) {
 if(!NativeAvatarEnabled())return;
 if(Range(ctx.r4.u32,4))rex::memory::store_and_swap<uint32_t>(NativeAvatarGuest(base,ctx.r4.u32),0);
 ctx.r3.u64=1; // Console behavior when instrumentation is disabled.
}
REX_HOOK_RAW(__imp__XamAvatarGetMetadataSignedOutProfileCount) {
 if(!NativeAvatarEnabled())return;
 const uint32_t output=ctx.r3.u32,overlap=ctx.r4.u32;
 const bool ok=Range(output,4);
 if(ok)rex::memory::store_and_swap<uint32_t>(NativeAvatarGuest(base,output),0);
 ctx.r3.u64=Complete(overlap,ok?0:0x80070057);
}
