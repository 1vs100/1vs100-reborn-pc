// onevs100 - ReXGlue Recompiled Project

#include "onevs100_init.h"
#include <vector>
extern "C" REX_FUNC(native_dialogue_play);
extern "C" REX_FUNC(native_debug_log);
extern "C" REX_FUNC(native_storage);

// Eight-byte virtual adjustor thunk omitted from the analyzer's function map.
extern "C" REX_FUNC(native_8236D880) {
  ctx.r3.s64 -= 36;
  sub_8236D428(ctx, base);
}
// Adjacent eight-instruction dispatchers at 82445560/80/A0 differ only in args.
extern "C" REX_FUNC(native_82445580) {
  ctx.r3.u64 = REX_LOAD_U32(ctx.r3.u32 + 332);
  ctx.r5.u64 = ctx.r4.u64;
  ctx.r6.s64 = 2;
  ctx.r4.s64 = 10;
  ctx.r11.u64 = REX_LOAD_U32(ctx.r3.u32);
  ctx.r11.u64 = REX_LOAD_U32(ctx.r11.u32 + 36);
  ctx.ctr.u64 = ctx.r11.u64;
  REX_CALL_INDIRECT_FUNC(ctx.ctr.u32);
}
extern "C" REX_FUNC(native_8214CF90) {
  ctx.r4.s64 = 1;
  sub_8214CDC8(ctx, base);
}
// Original instruction at each address is exactly blr (0x4E800020).
// Register the real empty virtual methods omitted by function discovery.
extern "C" REX_FUNC(native_8233F5B8) { }
extern "C" REX_FUNC(native_8233F5C0) { }
// Verified li r3,0; blr and adjacent blr-only virtual methods.
extern "C" REX_FUNC(native_8233C3C0) { ctx.r3.s64 = 0; }
extern "C" REX_FUNC(native_8233C3C8) { }
// Original six-instruction virtual dispatcher: lwz/addi/lwz/lwz/mtctr/bctr.
extern "C" REX_FUNC(native_82372B18) {
  ctx.r11.u64 = REX_LOAD_U32(ctx.r3.u32 + 72);
  ctx.r3.s64 = ctx.r11.s64 + 64;
  ctx.r11.u64 = REX_LOAD_U32(ctx.r11.u32 + 64);
  ctx.r11.u64 = REX_LOAD_U32(ctx.r11.u32 + 20);
  ctx.ctr.u64 = ctx.r11.u64;
  REX_CALL_INDIRECT_FUNC(ctx.ctr.u32);
}

const rex::PPCImageInfo& NativeImageConfig() {
  static std::vector<PPCFuncMapping> mappings = [] {
    std::vector<PPCFuncMapping> result;
    for (auto* p = PPCFuncMappings; p->guest; ++p) {
      auto mapping = *p;
      if (mapping.guest == 0x821A1A40) mapping.host = native_dialogue_play;
      result.push_back(mapping);
    }
    result.push_back({0x8236D880, native_8236D880});
    result.push_back({0x82445580, native_82445580});
    result.push_back({0x824455A0, sub_82445560});
    result.push_back({0x8214CF90, native_8214CF90});
    result.push_back({0x8233F5B8, native_8233F5B8});
    result.push_back({0x8233F5C0, native_8233F5C0});
    result.push_back({0x8233C3C0, native_8233C3C0});
    result.push_back({0x8233C3C8, native_8233C3C8});
    result.push_back({0x82372B18, native_82372B18});
    result.push_back({0x8299D428, native_debug_log});
    result.push_back({0x8299D430, native_storage});
    result.push_back({0, nullptr});
    return result;
  }();
  static rex::PPCImageInfo config = [] {
    auto result = PPCImageConfig;
    result.func_mappings = mappings.data();
    return result;
  }();
  return config;
}

#include "onevs100_app.h"

REX_DEFINE_APP(onevs100, Onevs100App::Create)
