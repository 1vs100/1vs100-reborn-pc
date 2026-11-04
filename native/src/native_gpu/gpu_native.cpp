// The title's own D3D12 renderer for desktop Windows.
//
// It replaces the rexgpu-xenos plugin: the guest's ring buffer is not parsed as
// PM4. The edge hooks in this module record what the title's D3D runtime asks
// for, and the records are executed with plume on a D3D12 device that owns the
// window.
//
// This module owns the guest GPU (MMIO, ring, vblank) and hands the guest's D3D
// edge calls to the renderer in native/src/render, which owns the device and the
// swap chain. The renderer presents when the guest swaps.
#include "native_gpu/gpu_native.h"

#include "native_gpu/guest_gpu.h"

#include <atomic>
#include <chrono>
#include <thread>
#include <vector>

#if defined(_WIN32)
#include <windows.h>
#endif

#include <rex/cvar.h>
#include <rex/logging.h>
#include <rex/runtime.h>
#include <rex/system/interfaces/graphics.h>
#include <rex/ui/window.h>
#include <rex/ui/windowed_app_context.h>
#include <rex/platform.h>

#if REX_PLATFORM_WINRT
#include <winrt/windows.foundation.h>
#include <winrt/windows.graphics.display.h>
#include <winrt/windows.ui.core.h>
#endif

#include <plume_d3d12.h>
#include <plume_render_interface.h>

#include "render/gpu.h"

REXCVAR_DEFINE_BOOL(native_renderer, false, "GPU",
                    "Use the title's own D3D12 renderer instead of the xenos plugin (Windows)");
REXCVAR_DEFINE_BOOL(native_renderer_present, true, "GPU",
                    "native_renderer: create the swap chain and present (off = guest GPU only)");

namespace onevs100::gpu {

// The X_STATUS_* macros expand to a cast to the unqualified type.
using rex::X_STATUS;

namespace {

#if defined(_WIN32)
// Logs a structured exception instead of letting it kill the process, so a
// failure inside the GPU backend names its code and address.
int LogSeh(const char* what, EXCEPTION_POINTERS* info) {
  const auto* record = info->ExceptionRecord;
  const uintptr_t address = reinterpret_cast<uintptr_t>(record->ExceptionAddress);
  const HMODULE base = GetModuleHandleW(nullptr);
  const uintptr_t offset = address - reinterpret_cast<uintptr_t>(base);
  REXKRNL_ERROR("native renderer: {} raised SEH code={:08X} address={:016X} (exe+{:X})", what,
                static_cast<uint32_t>(record->ExceptionCode), address, offset);
  return EXCEPTION_EXECUTE_HANDLER;
}
#endif

}  // namespace

class NativeGraphicsSystem final : public rex::system::IGraphicsSystem {
 public:
  NativeGraphicsSystem() = default;
  ~NativeGraphicsSystem() override { Shutdown(); }

  X_STATUS SetupPresentation(rex::ui::WindowedAppContext* app_context) override {
    if (!app_context) {
      return X_STATUS_INVALID_PARAMETER;
    }
    app_context_ = app_context;
    return X_STATUS_SUCCESS;
  }

  X_STATUS SetupGuestGpu(rex::runtime::FunctionDispatcher* function_dispatcher,
                                       rex::system::KernelState* kernel_state) override {
    dispatcher_ = function_dispatcher;
    kernel_state_ = kernel_state;

    // The guest GPU is ours: MMIO registers, ring writeback and the vblank
    // interrupt. The ring is never parsed; draws arrive through the edge hooks.
    guest_gpu_ = std::make_unique<GuestGpu>();
    if (!guest_gpu_->Initialize(function_dispatcher, kernel_state)) {
      REXLOG_ERROR("native renderer: guest GPU setup failed");
      guest_gpu_.reset();
      return X_STATUS_UNSUCCESSFUL;
    }

    if (!REXCVAR_GET(native_renderer_present)) {
      REXKRNL_INFO("native renderer: presentation disabled by native_renderer_present");
      return X_STATUS_SUCCESS;
    }
    if (auto* runtime = rex::Runtime::instance()) {
      window_ = runtime->display_window();
    }
    StartRenderer();
    return X_STATUS_SUCCESS;
  }

  bool has_presentation() const override {
    return render::Initialized();
  }

  void SetInterruptCallback(uint32_t callback, uint32_t user_data) override {
    interrupt_callback_ = callback;
    interrupt_data_ = user_data;
    if (guest_gpu_) {
      guest_gpu_->SetInterruptCallback(callback, user_data);
    }
  }

  void InitializeRingBuffer(uint32_t ptr, uint32_t size_log2) override {
    ring_buffer_ptr_ = ptr;
    ring_buffer_size_log2_ = size_log2;
    if (guest_gpu_) {
      guest_gpu_->InitializeRingBuffer(ptr, size_log2);
    }
  }

  void EnableReadPointerWriteBack(uint32_t ptr, uint32_t block_size_log2) override {
    ring_read_ptr_ = ptr;
    ring_block_size_log2_ = block_size_log2;
    if (guest_gpu_) {
      guest_gpu_->EnableReadPointerWriteBack(ptr, block_size_log2);
    }
  }

  void InitializeShaderStorage(const std::filesystem::path& cache_root, uint32_t title_id,
                               bool blocking) override {
    // No persistent pipeline cache yet; the renderer compiles what it needs.
    (void)cache_root;
    (void)title_id;
    (void)blocking;
  }

  void Shutdown() override {
    StopRenderer();
    if (guest_gpu_) {
      guest_gpu_->Shutdown();
      guest_gpu_.reset();
    }
    window_ = nullptr;
  }

  // Starts the renderer on the window. Runs on the UI thread: UWP requires the
  // CoreWindow's own thread to create the swap chain for it.
  void StartRenderer() {
    plume::RenderWindow handle = nullptr;
    uint32_t width = 0;
    uint32_t height = 0;
    if (!DescribeWindow(&handle, &width, &height)) {
      return;
    }
    plume::SetLogSink([](const char* message) { REXKRNL_ERROR("native renderer: {}", message); });
    if (!render::Initialize(handle, width, height)) {
      REXLOG_ERROR("native renderer: the renderer did not start");
      return;
    }
    REXKRNL_INFO("native renderer: device and swap chain ready");
  }

 private:
  // Fills in the swap chain target for the current platform.
  bool DescribeWindow(plume::RenderWindow* handle, uint32_t* width, uint32_t* height) {
#if REX_PLATFORM_WINRT
    // SDL owns a CoreWindow; the swap chain must be created for it, not for an
    // HWND. Bounds are in DIPs, the back buffer is in pixels.
    auto core_window = winrt::Windows::UI::Core::CoreWindow::GetForCurrentThread();
    if (!core_window) {
      REXLOG_ERROR("native renderer: no CoreWindow on this thread");
      return false;
    }
    const auto bounds = core_window.Bounds();
    if (bounds.Width <= 0.0f || bounds.Height <= 0.0f) {
      REXLOG_ERROR("native renderer: CoreWindow has no size");
      return false;
    }
    float scale = 1.0f;
    if (auto display = winrt::Windows::Graphics::Display::DisplayInformation::GetForCurrentView()) {
      scale = display.LogicalDpi() / 96.0f;
    }
    *handle = static_cast<plume::RenderWindow>(winrt::get_abi(core_window));
    *width = static_cast<uint32_t>(bounds.Width * scale);
    *height = static_cast<uint32_t>(bounds.Height * scale);
#else
    if (!window_) {
      REXLOG_INFO("native renderer: no window yet; not presenting");
      return false;
    }
    void* native_handle = window_->GetNativeWindowHandle();
    if (!native_handle) {
      REXLOG_INFO("native renderer: no window handle yet; not presenting");
      return false;
    }
    *handle = static_cast<plume::RenderWindow>(native_handle);
#endif
    return true;
  }

  void StopRenderer() {
    render::Shutdown();
  }

  rex::ui::WindowedAppContext* app_context_ = nullptr;
  rex::ui::Window* window_ = nullptr;
  rex::runtime::FunctionDispatcher* dispatcher_ = nullptr;
  rex::system::KernelState* kernel_state_ = nullptr;

  uint32_t interrupt_callback_ = 0;
  uint32_t interrupt_data_ = 0;
  uint32_t ring_buffer_ptr_ = 0;
  uint32_t ring_buffer_size_log2_ = 0;
  uint32_t ring_read_ptr_ = 0;
  uint32_t ring_block_size_log2_ = 0;

  std::unique_ptr<GuestGpu> guest_gpu_;
};

std::unique_ptr<rex::system::IGraphicsSystem> CreateNativeGraphicsSystem() {
  return std::make_unique<NativeGraphicsSystem>();
}

}  // namespace onevs100::gpu
