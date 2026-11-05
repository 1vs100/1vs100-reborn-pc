// Title CRT and library routines that run as host calls. The translated loops copy one PPC register at a time; the host
// versions use the full vector width. A range that crosses into the 4 KB-offset physical window keeps the guest
// routine, so the address rule stays the codegen's own.
#include "onevs100_init.h"
#include <rex/cvar.h>
#include <rex/hook.h>

#include <algorithm>
#include <atomic>
#include <cstring>

#include "guest_math.h"
#include "guest_memory_ops.h"

extern "C" REX_FUNC(__imp__sub_824AF050);
extern "C" REX_FUNC(__imp__sub_824AF580);
extern "C" REX_FUNC(__imp__sub_823252E8);
extern "C" REX_FUNC(__imp__sub_824AF4D8);

REXCVAR_DEFINE_BOOL(guest_crt_hooks, true, "Kernel", "Run the title's CRT memcpy, memset, floor and ceil as host calls");

namespace {

constexpr uint32_t kPhysicalWindow = 0xE0000000u;

// The guest copies run strictly forward, so an overlapping copy smears its source (titles fill a pattern that way);
// those keep the guest routine.
bool Overlap(uint32_t dst, uint32_t src, uint32_t size) {
  return dst < src + uint64_t(size) && src < dst + uint64_t(size);
}

// One host span for [address, address + size): false when the range wraps or straddles the physical window.
bool HostSpan(uint32_t address, uint32_t size) {
  const uint64_t end = uint64_t(address) + size;
  if (end > 0x100000000ull) return false;
  return (address >= kPhysicalWindow) == (end - 1 >= kPhysicalWindow);
}

}  // namespace

// CRT memcpy(dst, src, size) -> dst
REX_HOOK_RAW(sub_824AF050) {
  const uint32_t dst = ctx.r3.u32, src = ctx.r4.u32, size = ctx.r5.u32;
  if (!size) return;
  if (!REXCVAR_GET(guest_crt_hooks) || Overlap(dst, src, size) || !HostSpan(dst, size) || !HostSpan(src, size)) {
    return __imp__sub_824AF050(ctx, base);
  }
  std::memcpy(REX_RAW_ADDR(dst), REX_RAW_ADDR(src), size);
}

// XAPI memcpy(dst, src, size) -> dst
REX_HOOK_RAW(sub_823252E8) {
  const uint32_t dst = ctx.r3.u32, src = ctx.r4.u32, size = ctx.r5.u32;
  if (!size) return;
  if (!REXCVAR_GET(guest_crt_hooks) || Overlap(dst, src, size) || !HostSpan(dst, size) || !HostSpan(src, size)) {
    return __imp__sub_823252E8(ctx, base);
  }
  std::memcpy(REX_RAW_ADDR(dst), REX_RAW_ADDR(src), size);
}

// CRT memset(dst, value, size) -> dst
REX_HOOK_RAW(sub_824AF580) {
  const uint32_t dst = ctx.r3.u32, size = ctx.r5.u32;
  if (!size) return;
  if (!REXCVAR_GET(guest_crt_hooks) || !HostSpan(dst, size)) return __imp__sub_824AF580(ctx, base);
  std::memset(REX_RAW_ADDR(dst), int(ctx.r4.u8), size);
}

// CRT memcpy for unaligned sources (byte and word loops) -> dst
REX_HOOK_RAW(sub_824AF4D8) {
  const uint32_t dst = ctx.r3.u32, src = ctx.r4.u32, size = ctx.r5.u32;
  if (!size) return;
  if (!REXCVAR_GET(guest_crt_hooks) || Overlap(dst, src, size) || !HostSpan(dst, size) || !HostSpan(src, size)) {
    return __imp__sub_824AF4D8(ctx, base);
  }
  std::memcpy(REX_RAW_ADDR(dst), REX_RAW_ADDR(src), size);
}

// CRT floor and ceil (f1 -> f1). The guest versions truncate through fctidz below 2^52 and return larger magnitudes,
// zeros and NaNs unchanged, like the host functions; native/tests/floor_ceil_test.cpp checks them bit for bit.
extern "C" REX_FUNC(__imp__sub_824B2CF0);
extern "C" REX_FUNC(__imp__sub_824B0960);
REX_HOOK_RAW(sub_824B2CF0) {
  if (!REXCVAR_GET(guest_crt_hooks)) return __imp__sub_824B2CF0(ctx, base);
  ctx.f1.f64 = std::floor(ctx.f1.f64);
}
REX_HOOK_RAW(sub_824B0960) {
  if (!REXCVAR_GET(guest_crt_hooks)) return __imp__sub_824B0960(ctx, base);
  ctx.f1.f64 = onevs100::GuestCeil(ctx.f1.f64);
}

// XMemSet128(dst, value, size): fills size / 128 whole cache lines.
extern "C" REX_FUNC(__imp__sub_82324EB8);
REX_HOOK_RAW(sub_82324EB8) {
  const uint32_t dst = ctx.r3.u32, size = ctx.r5.u32 & ~127u;
  if (!size) return;
  if (!REXCVAR_GET(guest_crt_hooks) || !HostSpan(dst, size)) return __imp__sub_82324EB8(ctx, base);
  std::memset(REX_RAW_ADDR(dst), int(ctx.r4.u8), size);
}

REXCVAR_DEFINE_BOOL(guest_lib_hooks, true, "Kernel",
                    "Run the title's string compare, matrix multiply and reference counting as host calls");

namespace {

uint32_t LoadBe32(uint8_t* base, uint32_t address) {
  uint32_t value;
  std::memcpy(&value, REX_RAW_ADDR(address), 4);
  return __builtin_bswap32(value);
}

void StoreBe32(uint8_t* base, uint32_t address, uint32_t value) {
  value = __builtin_bswap32(value);
  std::memcpy(REX_RAW_ADDR(address), &value, 4);
}

}  // namespace

// std::string::compare(this, pos, count, const char* other, other_count) -> <0, 0, >0. A position past the end keeps
// the guest routine, which throws out_of_range.
extern "C" REX_FUNC(__imp__sub_8211FA40);
REX_HOOK_RAW(sub_8211FA40) {
  const uint32_t self = ctx.r3.u32, pos = ctx.r4.u32, other = ctx.r6.u32, other_count = ctx.r7.u32;
  if (!REXCVAR_GET(guest_lib_hooks) || !HostSpan(self, 28)) return __imp__sub_8211FA40(ctx, base);
  const uint32_t size = LoadBe32(base, self + 20), capacity = LoadBe32(base, self + 24);
  if (size < pos) return __imp__sub_8211FA40(ctx, base);
  const uint32_t count = std::min(ctx.r5.u32, size - pos);
  const uint32_t n = std::min(count, other_count);
  const uint32_t data = (capacity >= 16 ? LoadBe32(base, self + 4) : self + 4) + pos;
  if (n && (!HostSpan(data, n) || !HostSpan(other, n))) return __imp__sub_8211FA40(ctx, base);
  const auto* a = reinterpret_cast<const uint8_t*>(REX_RAW_ADDR(data));
  const auto* b = reinterpret_cast<const uint8_t*>(REX_RAW_ADDR(other));
  int32_t result = 0;
  for (uint32_t i = 0; i < n && !result; ++i) result = int32_t(a[i]) - int32_t(b[i]);
  if (!result) result = count < other_count ? -1 : count != other_count;
  ctx.r3.s64 = result;
}

// The guest multiplies and adds in separate roundings; a contracted multiply-add would differ in the last bit.
#pragma STDC FP_CONTRACT OFF
// XuiMatrixMultiply(out, a, b): out = a * b for row-major 4x4 float matrices, any alignment. Every input is read
// before the first store, so out may alias a or b. The guest multiplies, then adds in the order below.
extern "C" REX_FUNC(__imp__sub_82456368);
REX_HOOK_RAW(sub_82456368) {
  const uint32_t out = ctx.r3.u32, a = ctx.r4.u32, b = ctx.r5.u32;
  if (!REXCVAR_GET(guest_lib_hooks) || !HostSpan(out, 64) || !HostSpan(a, 64) || !HostSpan(b, 64)) {
    return __imp__sub_82456368(ctx, base);
  }
  ctx.fpscr.enableFlushMode();
  float ma[16], mb[16], mo[16];
  for (int i = 0; i < 16; ++i) {
    const uint32_t wa = LoadBe32(base, a + 4 * i), wb = LoadBe32(base, b + 4 * i);
    std::memcpy(&ma[i], &wa, 4);
    std::memcpy(&mb[i], &wb, 4);
  }
  for (int row = 0; row < 4; ++row) {
    for (int col = 0; col < 4; ++col) {
      float sum = ma[row * 4] * mb[col];
      for (int k = 1; k < 4; ++k) sum = ma[row * 4 + k] * mb[k * 4 + col] + sum;
      mo[row * 4 + col] = sum;
    }
  }
  for (int i = 0; i < 16; ++i) {
    uint32_t word;
    std::memcpy(&word, &mo[i], 4);
    StoreBe32(base, out + 4 * i, word);
  }
}

namespace {

// Adds delta to the big-endian reference count at address and returns the new count.
uint32_t AddReference(uint8_t* base, uint32_t address, int32_t delta) {
  auto* word = reinterpret_cast<std::atomic<uint32_t>*>(REX_RAW_ADDR(address));
  uint32_t old = word->load(std::memory_order_relaxed), next;
  do {
    next = __builtin_bswap32(__builtin_bswap32(old) + uint32_t(delta));
  } while (!word->compare_exchange_weak(old, next, std::memory_order_acq_rel, std::memory_order_relaxed));
  return __builtin_bswap32(next);
}

}  // namespace

// AddRef(object) -> count after incrementing the word at +4.
extern "C" REX_FUNC(__imp__sub_828B08E0);
REX_HOOK_RAW(sub_828B08E0) {
  const uint32_t address = ctx.r3.u32 + 4;
  if (!REXCVAR_GET(guest_lib_hooks) || (address & 3) || !HostSpan(address, 4)) return __imp__sub_828B08E0(ctx, base);
  ctx.r3.u64 = AddReference(base, address, 1);
}

// Release(object) -> count after decrementing the word at +4; at zero the deleting destructor (vtable slot 0) runs
// with flag 1.
extern "C" REX_FUNC(__imp__sub_8211D400);
REX_HOOK_RAW(sub_8211D400) {
  const uint32_t self = ctx.r3.u32, address = self + 4;
  if (!REXCVAR_GET(guest_lib_hooks) || (address & 3) || !HostSpan(address, 4)) return __imp__sub_8211D400(ctx, base);
  const uint32_t count = AddReference(base, address, -1);
  if (!count && self) {
    const uint32_t destructor = LoadBe32(base, LoadBe32(base, self));
    ctx.r4.s64 = 1;
    ctx.lr = 0x8211D45C;
    REX_CALL_INDIRECT_FUNC(destructor);
  }
  ctx.r3.u64 = count;
}

REXCVAR_DEFINE_BOOL(guest_atomic_window_lock, false, "Kernel",
                    "Take the global lock around the title's interlocked sequences. They are host compare-exchanges "
                    "either way");

REXCVAR_DEFINE_BOOL(guest_keep_flush_mode, true, "Kernel",
                    "Keep denormal flushing on for scalar float code instead of switching it per instruction run");

namespace onevs100 {

void ConfigureGuestRuntime() {
  ppc_atomic_window_lock_() = REXCVAR_GET(guest_atomic_window_lock);
  rex::ppc::FPSCRRegister::keepFlushMode = REXCVAR_GET(guest_keep_flush_mode);
}

}  // namespace onevs100
