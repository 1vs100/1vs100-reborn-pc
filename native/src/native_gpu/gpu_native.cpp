// The title's own D3D12 renderer for desktop Windows.
//
// It replaces the rexgpu-xenos plugin: the guest's ring buffer is not parsed as
// PM4. The edge hooks in this module record what the title's D3D runtime asks
// for, and the records are executed with plume on a D3D12 device that owns the
// window.
//
// Milestone 1 (this file, today): own the window, create the device and swap
// chain, and present a cleared frame. Guest draws are not executed yet, so the
// window stays a solid colour while native_renderer is on.
#include "native_gpu/gpu_native.h"

#include <atomic>
#include <chrono>
#include <thread>
#include <vector>

#include <rex/cvar.h>
#include <rex/logging.h>
#include <rex/runtime.h>
#include <rex/system/interfaces/graphics.h>
#include <rex/ui/window.h>
#include <rex/ui/windowed_app_context.h>
#include <rex/platform.h>
#include <rex/system/gpu_plugin.h>

#if REX_PLATFORM_WINRT
#include <winrt/windows.foundation.h>
#include <winrt/windows.graphics.display.h>
#include <winrt/windows.ui.core.h>
#endif

#include <plume_render_interface.h>

// plume_d3d12.cpp defines this without declaring it in a header.
namespace plume {
std::unique_ptr<RenderInterface> CreateD3D12Interface();
}  // namespace plume

REXCVAR_DEFINE_BOOL(native_renderer, false, "GPU",
                    "Use the title's own D3D12 renderer instead of the xenos plugin (Windows)");

namespace onevs100::gpu {

// The X_STATUS_* macros expand to a cast to the unqualified type.
using rex::X_STATUS;

namespace {

using plume::RenderCommandList;
using plume::RenderCommandQueue;
using plume::RenderDevice;
using plume::RenderInterface;
using plume::RenderSwapChain;
using plume::RenderSwapChainDesc;

constexpr plume::RenderFormat kSwapChainFormat = plume::RenderFormat::R8G8B8A8_UNORM;

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

    // The guest GPU, MMIO and the command processor still come from the xenos
    // plugin, built headless so it does not touch the window. The renderer here
    // owns presentation while it grows into the full path.
    guest_gpu_ = rex::system::LoadGpuPlugin("xenos");
    if (!guest_gpu_) {
      REXLOG_ERROR("native renderer: headless xenos plugin failed to load");
      return X_STATUS_UNSUCCESSFUL;
    }
    const X_STATUS guest_status = guest_gpu_->SetupGuestGpu(function_dispatcher, kernel_state);
    if (XFAILED(guest_status)) {
      REXLOG_ERROR("native renderer: headless guest GPU setup failed: {:08X}",
                   static_cast<uint32_t>(guest_status));
      return guest_status;
    }

    if (auto* runtime = rex::Runtime::instance()) {
      window_ = runtime->display_window();
    }
    StartRenderThread();
    return X_STATUS_SUCCESS;
  }

  bool has_presentation() const override {
    return swap_chain_ != nullptr && !swap_chain_->isEmpty() && swap_chain_->getTextureCount() != 0;
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
    if (guest_gpu_) {
      guest_gpu_->InitializeShaderStorage(cache_root, title_id, blocking);
    }
  }

  void Shutdown() override {
    StopRenderThread();
    framebuffers_.clear();
    framebuffer_textures_.clear();
    fence_.reset();
    if (guest_gpu_) {
      guest_gpu_->Shutdown();
      guest_gpu_.reset();
    }
    swap_chain_.reset();
    queue_.reset();
    device_.reset();
    interface_.reset();
    window_ = nullptr;
  }

  // Creates the device and swap chain, then starts the render thread. Runs on
  // the UI thread: UWP requires the CoreWindow's own thread for the swap chain.
  void StartRenderThread() {
    if (render_thread_.joinable()) {
      return;
    }
    RenderSwapChainDesc desc;
    if (!DescribeWindow(&desc)) {
      return;
    }
    interface_ = plume::CreateD3D12Interface();
    if (!interface_) {
      REXLOG_ERROR("native renderer: no D3D12 interface");
      return;
    }
    device_ = interface_->createDevice();
    if (!device_) {
      REXLOG_ERROR("native renderer: no D3D12 device");
      return;
    }
    queue_ = device_->createCommandQueue(plume::RenderCommandListType::DIRECT);
    if (!queue_) {
      REXLOG_ERROR("native renderer: no command queue");
      return;
    }
    swap_chain_ = queue_->createSwapChain(desc);
    if (!swap_chain_ || swap_chain_->isEmpty() || swap_chain_->getTextureCount() == 0) {
      // plume returns the object even when the swap chain creation failed.
      REXLOG_ERROR("native renderer: no swap chain back buffers");
      swap_chain_.reset();
      queue_.reset();
      device_.reset();
      interface_.reset();
      return;
    }
    REXLOG_INFO("native renderer: D3D12 device and swap chain ready ({}x{})",
                swap_chain_->getWidth(), swap_chain_->getHeight());
    running_.store(true);
    render_thread_ = std::thread([this] { RenderLoop(); });
  }

 private:
  // Fills in the swap chain target for the current platform.
  bool DescribeWindow(RenderSwapChainDesc* desc) {
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
    desc->renderWindow = static_cast<plume::RenderWindow>(winrt::get_abi(core_window));
    desc->width = static_cast<uint32_t>(bounds.Width * scale);
    desc->height = static_cast<uint32_t>(bounds.Height * scale);
#else
    if (!window_) {
      REXLOG_INFO("native renderer: no window yet; not presenting");
      return false;
    }
    void* handle = window_->GetNativeWindowHandle();
    if (!handle) {
      REXLOG_INFO("native renderer: no window handle yet; not presenting");
      return false;
    }
    desc->renderWindow = static_cast<plume::RenderWindow>(handle);
#endif
    desc->format = kSwapChainFormat;
    desc->textureCount = 2;
    return true;
  }

  void StopRenderThread() {
    if (!running_.exchange(false)) {
      return;
    }
    if (render_thread_.joinable()) {
      render_thread_.join();
    }
  }

  void RenderLoop() {
    while (running_.load(std::memory_order_relaxed)) {
      RenderFrame();
      std::this_thread::sleep_for(std::chrono::milliseconds(16));
    }
  }

  // One framebuffer per back buffer, rebuilt when the swap chain hands out new
  // textures (plume caches the render target handles in the framebuffer).
  plume::RenderFramebuffer* FramebufferFor(plume::RenderTexture* back_buffer) {
    for (size_t i = 0; i < framebuffers_.size(); ++i) {
      if (framebuffer_textures_[i] == back_buffer) {
        return framebuffers_[i].get();
      }
    }
    const plume::RenderTexture* attachments[1] = {back_buffer};
    auto framebuffer =
        device_->createFramebuffer(plume::RenderFramebufferDesc(attachments, 1));
    if (!framebuffer) {
      return nullptr;
    }
    framebuffers_.push_back(std::move(framebuffer));
    framebuffer_textures_.push_back(back_buffer);
    return framebuffers_.back().get();
  }

  void RenderFrame() {
    if (!swap_chain_ || swap_chain_->needsResize()) {
      if (swap_chain_) {
        swap_chain_->resize();
      }
      return;
    }
    uint32_t texture_index = 0;
    if (!swap_chain_->acquireTexture(nullptr, &texture_index)) {
      return;
    }
    plume::RenderTexture* back_buffer = swap_chain_->getTexture(texture_index);
    if (!back_buffer) {
      return;
    }
    plume::RenderFramebuffer* framebuffer = FramebufferFor(back_buffer);
    if (!framebuffer) {
      return;
    }
    if (!fence_) {
      fence_ = device_->createCommandFence();
      if (!fence_) {
        return;
      }
    }
    auto command_list = queue_->createCommandList();
    if (!command_list) {
      return;
    }
    command_list->begin();
    plume::RenderTextureBarrier to_render_target(back_buffer, plume::RenderTextureLayout::COLOR_WRITE);
    command_list->barriers(plume::RenderBarrierStage::GRAPHICS, nullptr, 0, &to_render_target, 1);
    command_list->setFramebuffer(framebuffer);
    command_list->clearColor(0, plume::RenderColor(0.05f, 0.07f, 0.15f, 1.0f));
    plume::RenderTextureBarrier to_present(back_buffer, plume::RenderTextureLayout::PRESENT);
    command_list->barriers(plume::RenderBarrierStage::GRAPHICS, nullptr, 0, &to_present, 1);
    command_list->end();
    // The command list outlives the frame only until the GPU has finished with
    // it: its allocator is released with the list.
    queue_->executeCommandLists(command_list.get(), fence_.get());
    queue_->waitForCommandFence(fence_.get());
    swap_chain_->present(texture_index, nullptr, 0);
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

  std::unique_ptr<rex::system::IGraphicsSystem> guest_gpu_;
  std::unique_ptr<RenderInterface> interface_;
  std::unique_ptr<RenderDevice> device_;
  std::unique_ptr<RenderCommandQueue> queue_;
  std::unique_ptr<RenderSwapChain> swap_chain_;
  std::unique_ptr<plume::RenderCommandFence> fence_;
  std::vector<std::unique_ptr<plume::RenderFramebuffer>> framebuffers_;
  std::vector<plume::RenderTexture*> framebuffer_textures_;

  std::atomic<bool> running_{false};
  std::thread render_thread_;
};

std::unique_ptr<rex::system::IGraphicsSystem> CreateNativeGraphicsSystem() {
  return std::make_unique<NativeGraphicsSystem>();
}

}  // namespace onevs100::gpu
