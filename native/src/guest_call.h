#pragma once
// Calls into title code from a hook, on the calling guest thread's stack.
#include "onevs100_init.h"
#include <cstdint>
#include <initializer_list>
#include <span>

class GuestCall {
 public:
  // Scratch memory for arguments that point at data; it lives in this call's frame.
  static constexpr uint32_t kScratchSize = 0x200;

  GuestCall(PPCContext& parent, uint8_t* base);
  // Copies `bytes` into the scratch area and returns its guest address.
  uint32_t Scratch(std::span<const uint8_t> bytes);
  // Calls `target` with r3 upwards; arguments past the eighth go on the stack. Returns r3.
  uint64_t Call(uint32_t target, std::initializer_list<uint64_t> arguments);

 private:
  PPCContext& parent_;
  uint8_t* base_;
  uint32_t scratchUsed_ = 0;
};
