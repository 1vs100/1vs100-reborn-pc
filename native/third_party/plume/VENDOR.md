# plume (vendored)

- Upstream: https://github.com/renderbag/plume.git
- Commit: 4f556be ("Metal: Track resource residency at the device level (#101)")
- License: MIT (see LICENSE)
- Vendored: 2026-07-13 for the native video layer port
  (UnleashedRecomp-style guest D3D implementation).

Trims relative to upstream:
- examples/, contrib/metal-cpp, plume_metal.cpp/.mm dropped (no Apple build).
- contrib submodules flattened at their pinned commits:
  - D3D12MemoryAllocator 9ef66bc (include/ + src/D3D12MemAlloc.* only)
  - Vulkan-Headers 2fa2034 (include/ only, C headers only — the C++
    vulkan.hpp family is unused by plume and was 16MB)
  - VulkanMemoryAllocator 29b35ea (include/ only)
  - volk be3dbd4 (volk.c/.h + CMakeLists + LICENSE)

Note: the SDK's thirdparty/vulkan-headers, volk, vulkan-memory-allocator
(gated behind REXGLUE_USE_VULKAN) are intentionally NOT reused here — plume
expects its own pinned contrib paths and must build regardless of that
option. Keep this copy self-contained.

Local patch: CMakeLists.txt links d3d12+dxgi on WIN32 (upstream expects
the consuming app to do it).

Local patch: D3D12Device keeps an ID3D12PipelineLibrary in
%TEMP%\rexglue_d3d12_pipelines_<vendor>_<device>_<driver>.bin, loaded at
device creation and saved once a second while PSOs compile and on release
(the D3D12 counterpart of the Vulkan pipeline cache file).

Local patch: RenderCommandList::setBlendFactor (D3D12 OMSetBlendFactor, Vulkan
vkCmdSetBlendConstants); every Vulkan graphics pipeline declares
VK_DYNAMIC_STATE_BLEND_CONSTANTS so the constant blend colour is per draw.

Local patch: D3D12 stencil-plane texture views support preserving stencil contents when render targets resize.

Local patch: D3D12GraphicsPipeline copies RenderGraphicsPipelineDesc::stencilReference into stencilRef
(upstream leaves it 0, so OMSetStencilRef never saw the requested reference).

Local patch: the present-failure dump to plume_present_dbg.txt and the per-300-frame DXGI statistics line are
removed; shipped builds write no debug files.

Local patch: UWP. `PLUME_UWP` (from the consuming build) makes `RenderWindow` the
CoreWindow's `IUnknown`, creates the swap chain with `CreateSwapChainForCoreWindow`,
and skips `MakeWindowAssociation`; `RenderSwapChainDesc` carries an explicit back
buffer size because a CoreWindow cannot be queried from the render thread. The
approach follows worleydl/plume-uwp commit d40b98a.

Local patch: `plume_vulkan.cpp` is only compiled when `WIN32` is not set. It does
not build against the Store headers and Windows uses the D3D12 backend.

Local patch: the SDL Vulkan path (`PLUME_SDL_VULKAN_ENABLED`) targets SDL3: it links
`SDL3::Headers`, includes `<SDL3/SDL_vulkan.h>`, and uses the SDL3 signatures of
`SDL_Vulkan_CreateSurface` and `SDL_Vulkan_GetInstanceExtensions`. Linux builds use it, so
the window system (Wayland or X11) is SDL's choice.

Local patch: the SDL Vulkan option is offered on Apple as well, not just Linux. The Cocoa
path calls `CocoaWindow`, whose implementation (`plume_apple.mm`) this trim drops, so a
macOS build has to let SDL create the surface instead. `VulkanSwapChain::windowWrapper` is
declared only when the SDL path is off, which keeps that dropped file out of the link.
Configuring Apple with neither the Metal sources nor the SDL path stops with an error
instead of failing at link.

Local patch: failed `Present`, `ResizeBuffers` and `CreateGraphicsPipelineState` calls go through `plume::Report` with
their HRESULT and `GetDeviceRemovedReason()`; the renderer routes `Report` into the game log with `SetLogSink`.
