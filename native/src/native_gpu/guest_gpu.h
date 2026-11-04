#pragma once

#include <array>
#include <atomic>
#include <cstdint>

#include <rex/system/xthread.h>

namespace rex::runtime {
class FunctionDispatcher;
}
namespace rex::system {
class KernelState;
}

namespace onevs100::gpu {

// The guest's GPU, without a render backend.
//
// The title's D3D runtime drives the GPU through MMIO registers and a command
// ring. This class answers those registers and walks the ring in the vblank
// thread: it performs the memory writebacks the guest fences on (EVENT_WRITE_SHD)
// and honours WAIT_REG_MEM, then publishes the read pointer. Draw packets are
// skipped, not interpreted: the draws arrive through the edge hooks instead.
class GuestGpu {
 public:
  GuestGpu();
  ~GuestGpu();

  bool Initialize(rex::runtime::FunctionDispatcher* function_dispatcher,
                  rex::system::KernelState* kernel_state);
  void Shutdown();

  void SetInterruptCallback(uint32_t callback, uint32_t user_data);
  void InitializeRingBuffer(uint32_t ptr, uint32_t size_log2);
  void EnableReadPointerWriteBack(uint32_t ptr, uint32_t block_size_log2);

 private:
  static uint32_t ReadRegisterThunk(void* ppc_context, GuestGpu* gpu, uint32_t addr);
  static void WriteRegisterThunk(void* ppc_context, GuestGpu* gpu, uint32_t addr, uint32_t value);
  uint32_t ReadRegister(uint32_t addr);
  void WriteRegister(uint32_t addr, uint32_t value);

  void DispatchInterruptCallback(uint32_t source, uint32_t cpu);
  void VblankThreadMain();

  // Ring walk. The draws are not interpreted; the packets that matter are the
  // ones that write back to guest memory (fences) and the register writes the
  // guest can later poll.
  uint32_t RingDword(uint32_t index) const;
  uint32_t AdvanceRead(uint32_t index, uint32_t dwords) const;
  void WriteRingRegister(uint32_t index, uint32_t value);
  bool RingWaitRegMem(uint32_t index, uint32_t count);
  void WalkRing();

  // Draw and shader-load packets, decoded far enough to size the pipeline the
  // native renderer has to execute.
  void ObserveDraw(uint32_t index, uint32_t count, uint32_t initiator);
  void ObserveShaderLoad(uint32_t opcode, uint32_t index, uint32_t count);
  void LogDrawWindow();

  rex::runtime::FunctionDispatcher* function_dispatcher_ = nullptr;
  rex::system::KernelState* kernel_state_ = nullptr;

  std::atomic<uint32_t> interrupt_callback_{0};
  std::atomic<uint32_t> interrupt_callback_data_{0};

  uint32_t ring_buffer_ptr_ = 0;
  uint32_t ring_buffer_size_ = 0;
  uint32_t ring_dwords_ = 0;
  std::atomic<uint32_t> read_ptr_{0};
  uint32_t read_ptr_writeback_ptr_ = 0;
  uint32_t read_ptr_update_freq_ = 1;
  std::atomic<uint32_t> counter_{0};

  // The mapped window, 64 KB as dwords (indices 0..0x3FFF).
  std::array<uint32_t, 0x4000> registers_{};

  std::atomic<uint32_t> write_ptr_{0};
  std::atomic<uint32_t> submitted_{0};
  std::atomic<uint32_t> vblank_dispatched_{0};
  std::atomic<uint32_t> ring_writes_{0};
  std::atomic<uint32_t> skipped_packets_{0};
  std::atomic<uint32_t> wait_stalls_{0};

  struct DrawCounters {
    uint32_t draws;
    uint32_t indices;
    uint32_t indexed_draws;
    uint32_t bin_draws;
    uint32_t shader_loads;
    uint32_t primitives[64];
  };
  DrawCounters draw_counters_{};
  uint64_t draw_window_start_ticks_ = 0;
  uint32_t draw_windows_logged_ = 0;

  // A host thread bound to the kernel state: the interrupt dispatch needs a
  // guest thread state, which a plain std::thread does not have.
  rex::system::object_ref<rex::system::XHostThread> vblank_thread_;
  std::atomic<bool> vblank_running_{false};
};

}  // namespace onevs100::gpu
