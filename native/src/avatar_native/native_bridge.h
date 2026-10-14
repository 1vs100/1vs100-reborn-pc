#pragma once
#include <cstdint>
#include <cstring>
#include <rex/logging.h>
#include <rex/memory/utils.h>
#include <rex/system/xtypes.h>
#include "xna_avatar_format.h"
#include "native_tiling.h"
#ifdef NATIVE_AVATAR_CATALOG_TEST
#define XELOGW(...) ((void)0)
#define XELOGI(...) ((void)0)
#else
#define XELOGW(...) REXKRNL_WARN(__VA_ARGS__)
#define XELOGI(...) REXKRNL_INFO(__VA_ARGS__)
#endif
namespace xe {
template<class T> inline T align(T value,T alignment) { return (value+alignment-1)&~(alignment-1); }
template<class T> inline T load_and_swap(const void* p) { return rex::memory::load_and_swap<T>(p); }
template<class T> inline void store_and_swap(void* p,T value) { rex::memory::store_and_swap<T>(p,value); }
namespace kernel {
using X_RESULT=uint32_t;
using X_HRESULT=int32_t;
extern thread_local uint8_t* native_avatar_base;
// Guest addresses at or above 0xE0000000 (the 4 KB physical heap) live 0x1000
// bytes higher in host memory on Windows. Recompiled game code applies this via
// REX_PHYS_HOST_OFFSET / rex::memory::GuestPtr; the Avatar provider must apply
// exactly the same rule, or vertex/index/texture data written into the game's
// GPU buffer land 4 KB away from where the game and GPU read them.
// native_provider.cpp static_asserts agreement with rex::memory::detail.
constexpr uint32_t NativeAvatarHostOffset(uint32_t address) {
#if defined(_WIN32)
 return address>=0xE0000000u?0x1000u:0u;
#else
 return 0u;
#endif
}
inline uint8_t* NativeAvatarGuest(uint8_t* base,uint32_t address) { return base+address+NativeAvatarHostOffset(address); }
struct NativeAvatarMemory {
 template<class T> T TranslateVirtual(uint32_t address) { return reinterpret_cast<T>(native_avatar_base+address+NativeAvatarHostOffset(address)); }
};
inline NativeAvatarMemory* kernel_memory() { static NativeAvatarMemory memory;return &memory; }
namespace xna { avatar::Catalog* XnaAvatarCatalog(); }
namespace xam {
constexpr uint32_t kAvatarResultBufferSize=0x10000,kAvatarGpuBufferSize=0x200000;
void SetAvatarCoordinateSystem(uint32_t);
X_RESULT BuildAvatarAssets(const uint8_t*,size_t,uint32_t,uint32_t,uint32_t);
X_RESULT GenerateAvatarMipMaps(uint32_t,uint32_t,uint32_t);
X_RESULT LoadAvatarAnimation(const std::array<uint8_t,16>&,uint32_t,const std::string&);
}
}
}
