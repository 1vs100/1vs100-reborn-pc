// Native renderer entry points used by the app shell, the capture hooks and the scene driver.
#pragma once

#include <cstdint>
#include <vector>
#include <atomic>
#include <rex/logging.h>
#include <rex/system/kernel_state.h>
#include <filesystem>
#include <plume_render_interface_types.h>

#include <rex/ppc/context.h>

#include "render/gpu_records.h"


namespace onevs100::render {

// Creates the plume device and swap chain on the window and starts the render thread; false leaves the renderer off.
// UWP cannot query the CoreWindow from the render thread, so the back buffer size is passed in there.
bool Initialize(plume::RenderWindow window, uint32_t width = 0, uint32_t height = 0);

// True once the device and swap chain exist.
bool Initialized();

// True when the renderer was selected for this run, whether or not it has started
// yet. The edge hooks use this: the renderer starts on the presenting thread after
// the title has already begun submitting work, so the stand-in for the GPU has to
// act from the first call, while the plugin path must never be touched.
bool Enabled();

// TranslateVirtual never fails: unmapped addresses become wild host pointers
// that fault on first touch, so every edge read or write of guest memory must
// pass its address through here first. Low addresses are never valid.
inline bool ValidGuest(uint32_t address) {
  if (address < 0x10000) {
    return false;
  }
  const auto* memory = REX_KERNEL_MEMORY();
  return memory && memory->LookupHeap(address) != nullptr;
}

// Rate-limited report for a rejected guest address: silent skips would hide
// the next bad-address bug, but the edge runs hot so only the first few log.
inline void NoteBadGuest(uint32_t address, const char* site) {
  static std::atomic<uint32_t> noted{0};
  if (noted.fetch_add(1, std::memory_order_relaxed) < 8) {
    REXGPU_WARN("[gpu] ignoring unmapped guest address {:#x} at {}", address, site);
  }
}

// Stops the render thread, idles the device and destroys everything in reverse order; safe to call twice.
void Shutdown();

// The window's pixel size changed; the render thread rebuilds the swap chain before its next present.
void RequestResize();

// Guest thread: appends a record to the frame being built.
void Submit(Record record);

// Guest thread: hands the frame to the render thread, waiting while two frames are already queued.
void EndFrame();

// Folder holding the game's cardigan_bd font, used by the gpu_fps counter drawn at present.
void SetOverlayFontRoot(const std::filesystem::path& folder);

// Frames handed to the render thread so far; the capture keys per-frame upload coalescing on it.
uint64_t FrameIndex();

// Range keys the host dropped from shared memory since the last call; the guest thread forgets it sent them.
void TakeEvicted(std::vector<uint64_t>& keys);
bool AnyEvicted();

// First-frame hold until the start-up pipeline warm-up has compiled (bounded by gpu_pipeline_warmup_wait).
void WaitForWarmup();

// Vertical sync on the swap chain, applied by the host thread at its next present.
void SetVsync(bool enabled);

// The mouse pointer the host draws over the presented frame: position as a 0-1 fraction of the game image.
void SetPointer(float x, float y, bool visible);
// True while the game draws the hand itself (cc2::mouse::GloveTick); the baked sprite is skipped then.
void SuppressPointer(bool suppressed);

namespace edge {
// The title runtime's vertex-shader patch that rewrites the vfetch reads for the
// current vertex declaration. The draw path runs it on a scratch copy of the
// microcode so the shader lookup key is the patched form.
void PatchVertexShader(PPCContext& ctx, uint8_t* base);
void SetVertexShaderPatcher(void (*patch)(PPCContext& ctx, uint8_t* base));
}  // namespace edge

namespace draw {
// Guest thread: submits a Begin*Vertices draw whose End the game inlined; every commit point calls it first.
void FinishPending();
}  // namespace draw

}  // namespace onevs100::render
