// The runtime's variadic printf exports read stack arguments one word off
// (rex::system::format::StackArgList uses r1+0x54 as the start of 8-byte
// slots; the slots start at r1+0x50), so a call with more than eight register
// arguments formats garbage. These exports reuse the runtime's format engine
// with a correct argument reader.
#include <rex/hook.h>
#include <rex/memory/utils.h>
#include <rex/system/format.h>
#include <algorithm>
#include <cstring>

namespace {
namespace format = rex::system::format;

class GuestVarargs {
 public:
  GuestVarargs(PPCContext& ctx, uint8_t* base, int32_t first) : ctx_(ctx), base_(base), index_(first) {}
  uint32_t get32() { return static_cast<uint32_t>(get64()); }
  uint64_t get64() {
    const int32_t index = index_++;
    const uint64_t registers[8] = {ctx_.r3.u64, ctx_.r4.u64, ctx_.r5.u64, ctx_.r6.u64,
                                   ctx_.r7.u64, ctx_.r8.u64, ctx_.r9.u64, ctx_.r10.u64};
    if (index < 8) return registers[index];
    // Argument 9 onward: 8-byte big-endian slots from r1+0x50.
    return rex::memory::load_and_swap<uint64_t>(base_ + ctx_.r1.u32 + 0x50 + 8 * (index - 8));
  }

 private:
  PPCContext& ctx_;
  uint8_t* base_;
  int32_t index_;
};

// MSVC _snprintf rules: the NUL only fits below count; output beyond count fails with -1.
template <typename Unit, typename Store>
int32_t StoreBounded(const Unit* text, size_t length, uint32_t count, Store store) {
  const size_t written = std::min<size_t>(length, count);
  for (size_t i = 0; i < written; ++i) store(i, text[i]);
  if (length < count) store(length, Unit(0));
  return length > count ? -1 : static_cast<int32_t>(length);
}
}  // namespace

// sprintf(buffer, format, ...)
REX_HOOK_RAW(__imp__sprintf) {
  format::StringFormatData data(base + ctx.r4.u32);
  GuestVarargs args(ctx, base, 2);
  const int32_t count = format::format_core(base, data, args, false);
  if (count < 0) { ctx.r3.u64 = static_cast<uint64_t>(static_cast<int64_t>(count)); return; }
  std::memcpy(base + ctx.r3.u32, data.str().c_str(), data.str().size() + 1);
  ctx.r3.u64 = data.str().size();
}

// _snprintf(buffer, count, format, ...)
REX_HOOK_RAW(__imp___snprintf) {
  format::StringFormatData data(base + ctx.r5.u32);
  GuestVarargs args(ctx, base, 3);
  const int32_t count = format::format_core(base, data, args, false);
  if (count < 0) { ctx.r3.u64 = static_cast<uint64_t>(static_cast<int64_t>(count)); return; }
  uint8_t* buffer = base + ctx.r3.u32;
  const auto& text = data.str();
  ctx.r3.u64 = static_cast<uint64_t>(static_cast<int64_t>(
      StoreBounded(text.data(), text.size(), ctx.r4.u32, [&](size_t i, char c) { buffer[i] = static_cast<uint8_t>(c); })));
}

// swprintf(buffer, format, ...)
REX_HOOK_RAW(__imp__swprintf) {
  format::WideStringFormatData data(reinterpret_cast<const uint16_t*>(base + ctx.r4.u32));
  GuestVarargs args(ctx, base, 2);
  const int32_t count = format::format_core(base, data, args, true);
  if (count < 0) { ctx.r3.u64 = static_cast<uint64_t>(static_cast<int64_t>(count)); return; }
  const uint32_t buffer = ctx.r3.u32;
  const auto& text = data.wstr();
  for (size_t i = 0; i < text.size(); ++i) rex::memory::store_and_swap<uint16_t>(base + buffer + 2 * i, text[i]);
  rex::memory::store_and_swap<uint16_t>(base + buffer + 2 * text.size(), 0);
  ctx.r3.u64 = text.size();
}

// _snwprintf(buffer, count, format, ...)
REX_HOOK_RAW(__imp___snwprintf) {
  format::WideStringFormatData data(reinterpret_cast<const uint16_t*>(base + ctx.r5.u32));
  GuestVarargs args(ctx, base, 3);
  const int32_t count = format::format_core(base, data, args, true);
  if (count < 0) { ctx.r3.u64 = static_cast<uint64_t>(static_cast<int64_t>(count)); return; }
  const uint32_t buffer = ctx.r3.u32;
  const auto& text = data.wstr();
  ctx.r3.u64 = static_cast<uint64_t>(static_cast<int64_t>(StoreBounded(
      text.data(), text.size(), ctx.r4.u32,
      [&](size_t i, char16_t c) { rex::memory::store_and_swap<uint16_t>(base + buffer + 2 * i, c); })));
}
