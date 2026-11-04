#include "native_gpu/guest_gpu.h"

#include <algorithm>
#include <chrono>

#include <rex/chrono/clock.h>
#include <rex/kernel/xboxkrnl/video.h>
#include <rex/logging.h>
#include <rex/memory.h>
#include <rex/runtime.h>
#include <rex/system/function_dispatcher.h>
#include <rex/system/kernel_state.h>
#include <rex/system/xmemory.h>
#include <rex/system/xthread.h>
#include <rex/cvar.h>

namespace onevs100::gpu {

REXCVAR_DEFINE_BOOL(diag_guest_gpu, false, "GPU",
                    "DIAG: log guest GPU register writes and vblank dispatch");

namespace {

// xenos::Endian transform, identical to xenos::GpuSwap: kNone 0, k8in16 1,
// k8in32 2, k16in32 3.
uint32_t GpuSwap(uint32_t value, uint32_t endianness) {
  switch (endianness & 3) {
    case 1:
      return ((value << 8) & 0xFF00FF00) | ((value >> 8) & 0x00FF00FF);
    case 2:
      return rex::byte_swap(value);
    case 3:
      return ((value >> 16) & 0xFFFF) | (value << 16);
    default:
      return value;
  }
}

// GPU registers occupy 0x7FC80000-0x7FCFFFFF.
constexpr uint32_t kRegisterBase = 0x7FC80000;
constexpr uint32_t kRegisterMask = 0xFFFF0000;
constexpr uint32_t kRegisterSize = 0x10000;

// PM4 register indices (register index = byte offset / 4).
constexpr uint32_t kCpRbWPtr = 0x01C5;
constexpr uint32_t kRbEdramTiming = 0x0F00;
constexpr uint32_t kRbBcControl = 0x0F01;
constexpr uint32_t kD1ModeVCounter = 0x194C;
constexpr uint32_t kD1ModeInterruptStatus = 0x1951;
constexpr uint32_t kD1ModeViewportSize = 0x1961;

}  // namespace

GuestGpu::GuestGpu() = default;

GuestGpu::~GuestGpu() { Shutdown(); }

bool GuestGpu::Initialize(rex::runtime::FunctionDispatcher* function_dispatcher,
                          rex::system::KernelState* kernel_state) {
  function_dispatcher_ = function_dispatcher;
  kernel_state_ = kernel_state;
  if (!function_dispatcher_ || !kernel_state_) {
    return false;
  }

  auto* memory = kernel_state_->memory();
  if (!memory->AddVirtualMappedRange(
          kRegisterBase, kRegisterMask, kRegisterSize, this,
          reinterpret_cast<rex::runtime::MMIOReadCallback>(ReadRegisterThunk),
          reinterpret_cast<rex::runtime::MMIOWriteCallback>(WriteRegisterThunk))) {
    REXKRNL_ERROR("native renderer: could not map GPU registers");
    return false;
  }

  vblank_running_.store(true);
  vblank_thread_ = rex::system::object_ref<rex::system::XHostThread>(
      new rex::system::XHostThread(kernel_state_, 128 * 1024, 0, [this]() {
        VblankThreadMain();
        return 0;
      }));
  vblank_thread_->set_name("GPU VSync");
  vblank_thread_->Create();
  REXKRNL_INFO("native renderer: guest GPU registers mapped, vblank running");
  return true;
}

void GuestGpu::Shutdown() {
  vblank_running_.store(false);
  if (vblank_thread_) {
    vblank_thread_->Wait(0, 0, 0, nullptr);
    vblank_thread_.reset();
  }
}

void GuestGpu::SetInterruptCallback(uint32_t callback, uint32_t user_data) {
  interrupt_callback_.store(callback);
  interrupt_callback_data_.store(user_data);
  REXKRNL_INFO("native renderer: graphics interrupt callback {:08X} data {:08X}", callback,
               user_data);
}

void GuestGpu::InitializeRingBuffer(uint32_t ptr, uint32_t size_log2) {
  ring_buffer_ptr_ = ptr;
  ring_buffer_size_ = uint32_t(1) << (size_log2 + 3);
  ring_dwords_ = ring_buffer_size_ / 4;
  read_ptr_.store(0);
  write_ptr_.store(0);
  REXKRNL_INFO("native renderer: ring buffer at {:08X}, {} bytes", ptr, ring_buffer_size_);
}

void GuestGpu::EnableReadPointerWriteBack(uint32_t ptr, uint32_t block_size_log2) {
  read_ptr_writeback_ptr_ = ptr;
  read_ptr_update_freq_ = std::max(uint32_t(1), uint32_t(1) << block_size_log2 >> 2);
  REXKRNL_INFO("native renderer: ring read pointer writeback at {:08X}", ptr);
}

uint32_t GuestGpu::ReadRegisterThunk(void* ppc_context, GuestGpu* gpu, uint32_t addr) {
  (void)ppc_context;
  return gpu->ReadRegister(addr);
}

void GuestGpu::WriteRegisterThunk(void* ppc_context, GuestGpu* gpu, uint32_t addr,
                                  uint32_t value) {
  (void)ppc_context;
  gpu->WriteRegister(addr, value);
}

uint32_t GuestGpu::ReadRegister(uint32_t addr) {
  const uint32_t r = (addr & 0xFFFF) / 4;
  switch (r) {
    case kRbEdramTiming:
      return 0x08100748;
    case kRbBcControl:
      return 0x0000200E;
    case kD1ModeVCounter: {
      rex::system::X_VIDEO_MODE video_mode;
      rex::kernel::xboxkrnl::VdQueryVideoMode(&video_mode);
      return std::min(uint32_t(video_mode.display_height), uint32_t(0x0FFF));
    }
    case kD1ModeInterruptStatus:
      return 1;  // vblank
    case kD1ModeViewportSize: {
      rex::system::X_VIDEO_MODE video_mode;
      rex::kernel::xboxkrnl::VdQueryVideoMode(&video_mode);
      const uint32_t width = std::min(uint32_t(video_mode.display_width), uint32_t(0x0FFF));
      const uint32_t height = std::min(uint32_t(video_mode.display_height), uint32_t(0x0FFF));
      return (width << 16) | height;
    }
    default:
      break;
  }
  return registers_[r];
}

void GuestGpu::WriteRegister(uint32_t addr, uint32_t value) {
  const uint32_t r = (addr & 0xFFFF) / 4;
  if (REXCVAR_GET(diag_guest_gpu)) {
    static std::atomic<uint32_t> seen{0};
    const uint32_t n = seen.fetch_add(1, std::memory_order_relaxed);
    if (n < 48) {
      REXKRNL_INFO("DIAG gpu reg write {:04X} = {:08X}", r, value);
    }
  }
  registers_[r] = value;
  if (r != kCpRbWPtr) {
    return;
  }

  // The ring is not parsed, so every submitted write pointer counts as
  // consumed. Writing it back is what lets the guest's fences retire.
  write_ptr_.store(value);
  submitted_.fetch_add(1, std::memory_order_relaxed);
  ring_writes_.fetch_add(1, std::memory_order_relaxed);
  if (!read_ptr_writeback_ptr_ || !kernel_state_) {
    return;
  }
  auto* writeback =
      kernel_state_->memory()->TranslatePhysical<uint32_t*>(read_ptr_writeback_ptr_);
  if (!writeback) {
    return;
  }
  // The writeback is the ring's read pointer as a dword index. The guest stores
  // and reads it big-endian.
  *writeback = rex::byte_swap(read_ptr_.load());
}

uint32_t GuestGpu::RingDword(uint32_t index) const {
  if (!ring_dwords_) {
    return 0;
  }
  const uint32_t byte_offset = (index % ring_dwords_) * 4;
  auto* raw = kernel_state_->memory()->TranslatePhysical<uint32_t*>(ring_buffer_ptr_ + byte_offset);
  return raw ? rex::byte_swap(*raw) : 0;
}

uint32_t GuestGpu::AdvanceRead(uint32_t index, uint32_t dwords) const {
  return ring_dwords_ ? (index + dwords) % ring_dwords_ : 0;
}

void GuestGpu::WriteRingRegister(uint32_t index, uint32_t value) {
  if (index < registers_.size()) {
    registers_[index] = value;
  }
}

bool GuestGpu::RingWaitRegMem(uint32_t index, uint32_t count) {
  if (count < 5) {
    return true;
  }
  const uint32_t wait_info = RingDword(index + 1);
  uint32_t poll_addr = RingDword(index + 2);
  const uint32_t ref = RingDword(index + 3);
  const uint32_t mask = RingDword(index + 4);
  const bool is_memory = (wait_info & 0x10) != 0;

  uint32_t value = 0;
  if (is_memory) {
    const uint32_t endian = poll_addr & 3;
    poll_addr &= ~3u;
    auto* raw = kernel_state_->memory()->TranslatePhysical<uint32_t*>(poll_addr);
    if (!raw) {
      return true;
    }
    value = GpuSwap(*raw, endian);
  } else {
    value = poll_addr < registers_.size() ? registers_[poll_addr] : 0;
  }

  switch (wait_info & 0x7) {
    case 0x1:
      return (value & mask) < ref;
    case 0x2:
      return (value & mask) <= ref;
    case 0x3:
      return (value & mask) == ref;
    case 0x4:
      return (value & mask) != ref;
    case 0x5:
      return (value & mask) >= ref;
    case 0x6:
      return (value & mask) > ref;
    case 0x7:  // Always.
      return true;
    default:
      return false;
  }
}

void GuestGpu::WalkRing() {
  if (!ring_dwords_ || !ring_buffer_ptr_ || !read_ptr_writeback_ptr_) {
    return;
  }
  const uint32_t write = write_ptr_.load(std::memory_order_relaxed);
  if (write == 0xBAADF00D) {
    return;
  }
  uint32_t read = read_ptr_.load();
  uint32_t guard = 0;
  while (read != write && guard++ < 4096) {
    const uint32_t packet = RingDword(read);
    if (!packet) {
      // Padding is a nop; skipping it is what the SDK's parser does.
      read = AdvanceRead(read, 1);
      continue;
    }
    const uint32_t type = packet >> 30;
    if (type == 0) {
      const uint32_t count = ((packet >> 16) & 0x3FFF) + 1;
      const uint32_t base = packet & 0x7FFF;
      const bool one_register = (packet >> 15) & 1;
      for (uint32_t m = 0; m < count; ++m) {
        WriteRingRegister(one_register ? base : base + m, RingDword(read + 1 + m));
      }
      read = AdvanceRead(read, 1 + count);
    } else if (type == 1) {
      WriteRingRegister(packet & 0x7FF, RingDword(read + 1));
      WriteRingRegister((packet >> 11) & 0x7FF, RingDword(read + 2));
      read = AdvanceRead(read, 3);
    } else if (type == 2) {
      read = AdvanceRead(read, 1);
    } else {
      const uint32_t opcode = (packet >> 8) & 0x7F;
      const uint32_t count = ((packet >> 16) & 0x3FFF) + 1;
      if (opcode == 0x58 && count >= 3) {
        // EVENT_WRITE_SHD: the fence the guest polls.
        const uint32_t initiator = RingDword(read + 1);
        uint32_t address = RingDword(read + 2);
        const uint32_t value = RingDword(read + 3);
        uint32_t data = ((initiator >> 31) & 1) ? counter_.load() : value;
        const uint32_t endian = address & 3;
        address &= ~3u;
        if (REXCVAR_GET(diag_guest_gpu)) {
          REXKRNL_INFO("DIAG gpu EVENT_WRITE_SHD addr={:08X} value={:08X} counter={} endian={}",
                       address, data, (initiator >> 31) & 1, endian);
        }
        if (auto* dst = kernel_state_->memory()->TranslatePhysical<uint32_t*>(address)) {
          *dst = GpuSwap(data, endian);
        }
      } else if (opcode == 0x3C) {
        if (!RingWaitRegMem(read, count)) {
          if (REXCVAR_GET(diag_guest_gpu)) {
            wait_stalls_.fetch_add(1, std::memory_order_relaxed);
          }
          break;
        }
      } else {
        skipped_packets_.fetch_add(1, std::memory_order_relaxed);
      }
      read = AdvanceRead(read, 1 + count);
    }
  }
  read_ptr_.store(read);
  if (auto* writeback =
          kernel_state_->memory()->TranslatePhysical<uint32_t*>(read_ptr_writeback_ptr_)) {
    *writeback = rex::byte_swap(read_ptr_.load());
  }
}

void GuestGpu::DispatchInterruptCallback(uint32_t source, uint32_t cpu) {
  if (REXCVAR_GET(diag_guest_gpu)) {
    vblank_dispatched_.fetch_add(1, std::memory_order_relaxed);
  }
  const uint32_t callback = interrupt_callback_.load();
  if (!callback || !function_dispatcher_) {
    return;
  }
  auto* thread = rex::system::XThread::GetCurrentThread();
  if (!thread) {
    return;
  }
  // 0xFFFFFFFF means "any CPU"; the SDK's vblank uses 2.
  if (cpu == 0xFFFFFFFF) {
    cpu = 2;
  }
  thread->SetActiveCpu(static_cast<uint8_t>(cpu));
  uint64_t args[] = {source, interrupt_callback_data_.load()};
  function_dispatcher_->ExecuteInterrupt(thread->thread_state(), callback, args, rex::countof(args));
}

void GuestGpu::VblankThreadMain() {
  rex::system::X_VIDEO_MODE video_mode;
  rex::kernel::xboxkrnl::VdQueryVideoMode(&video_mode);
  const double refresh_rate_hz = std::max(1.0, double(float(video_mode.refresh_rate)));
  const uint64_t guest_tick_frequency = rex::chrono::Clock::guest_tick_frequency();
  const uint64_t vsync_interval_ticks =
      std::max(uint64_t(1), uint64_t(double(guest_tick_frequency) / refresh_rate_hz));
  uint64_t last_frame_time = rex::chrono::Clock::QueryGuestTickCount();

  while (vblank_running_.load(std::memory_order_relaxed)) {
    const uint64_t current_time = rex::chrono::Clock::QueryGuestTickCount();
    while (current_time - last_frame_time >= vsync_interval_ticks) {
      // The counter is what a counter-mode EVENT_WRITE_SHD writes back.
      counter_.fetch_add(1, std::memory_order_relaxed);
      DispatchInterruptCallback(0, 2);
      last_frame_time += vsync_interval_ticks;
    }
    WalkRing();
    if (REXCVAR_GET(diag_guest_gpu)) {
      const uint32_t dispatched = vblank_dispatched_.exchange(0);
      const uint32_t stalls = wait_stalls_.exchange(0);
      const uint32_t skipped = skipped_packets_.exchange(0);
      if (dispatched || stalls || skipped) {
        REXKRNL_INFO("DIAG gpu vblanks={} ring_writes={} read={} write={} waits_stalled={} skipped={}",
                     dispatched, ring_writes_.exchange(0), read_ptr_.load(),
                     write_ptr_.load(), stalls, skipped);
      }
    }
    std::this_thread::sleep_for(std::chrono::milliseconds(1));
  }
}

}  // namespace onevs100::gpu
