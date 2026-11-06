#include "guest_call.h"
#include "fatal_dialog.h"
#include <rex/hook.h>
#include <rex/logging.h>
#include <rex/memory/utils.h>
#include <cstring>

namespace {
// The callee frame: back chain and linkage area, then stack argument slots at
// +0x50, then the scratch area. The callee's own frame lies below.
constexpr uint32_t kStackArgumentBase = 0x50;
constexpr uint32_t kMaxStackArguments = 8;
constexpr uint32_t kScratchOffset = kStackArgumentBase + 8 * kMaxStackArguments;
constexpr uint32_t kFrameSize = kScratchOffset + GuestCall::kScratchSize;
constexpr size_t kRegisterArguments = 8;
}  // namespace

GuestCall::GuestCall(PPCContext& parent, uint8_t* base) : parent_(parent), base_(base) {}

uint32_t GuestCall::Scratch(std::span<const uint8_t> bytes) {
  const uint32_t size = (static_cast<uint32_t>(bytes.size()) + 7) & ~7u;
  if (scratchUsed_ + size > kScratchSize) {
    REXKRNL_CRITICAL("GuestCall scratch area exhausted");
    fatal_dialog::FatalError("1 vs 100", "GuestCall scratch area exhausted");
    std::abort();
  }
  const uint32_t address = parent_.r1.u32 - kFrameSize + kScratchOffset + scratchUsed_;
  std::memcpy(base_ + address, bytes.data(), bytes.size());
  scratchUsed_ += size;
  return address;
}

uint64_t GuestCall::Call(uint32_t target, std::initializer_list<uint64_t> arguments) {
  if (arguments.size() > kRegisterArguments + kMaxStackArguments) {
    REXKRNL_CRITICAL("GuestCall with {} arguments", arguments.size());
    fatal_dialog::FatalError("1 vs 100", "GuestCall with too many arguments");
    std::abort();
  }
  rex::CallFrame frame(parent_);
  frame.ctx.r1.u32 = parent_.r1.u32 - kFrameSize;
  // Back chain, as a real call frame starts.
  rex::memory::store_and_swap<uint32_t>(base_ + frame.ctx.r1.u32, parent_.r1.u32);
  PPCRegister* registers[kRegisterArguments] = {&frame.ctx.r3, &frame.ctx.r4, &frame.ctx.r5, &frame.ctx.r6,
                                                &frame.ctx.r7, &frame.ctx.r8, &frame.ctx.r9, &frame.ctx.r10};
  size_t index = 0;
  for (const uint64_t argument : arguments) {
    if (index < kRegisterArguments)
      registers[index]->u64 = argument;
    else
      rex::memory::store_and_swap<uint64_t>(
          base_ + frame.ctx.r1.u32 + kStackArgumentBase + 8 * static_cast<uint32_t>(index - kRegisterArguments),
          argument);
    ++index;
  }
  frame.ctx.ctr.u64 = target;
  {
    // The macro calls through the names ctx and base.
    PPCContext& ctx = frame.ctx;
    uint8_t* base = base_;
    REX_CALL_INDIRECT_FUNC(ctx.ctr.u32);
  }
  return frame.ctx.r3.u64;
}
