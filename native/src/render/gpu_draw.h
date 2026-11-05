// Draw capture entry points shared with the command-buffer replay: the D3D draw arguments, and building a DrawRecord
// from a device struct that is not the live one.
#pragma once

#include <cstdint>
#include <vector>

#include <rex/graphics/xenos.h>
#include <rex/ppc/context.h>

#include "render/gpu_records.h"

namespace onevs100::render::guest {
struct D3DDevice;
}

namespace onevs100::render::draw {

struct DrawArgs {
  uint32_t device = 0;
  rex::graphics::xenos::PrimitiveType type = rex::graphics::xenos::PrimitiveType::kNone;
  bool indexed = false;
  uint32_t start = 0;          // StartVertex, or StartIndex for indexed draws
  int32_t base_vertex = 0;     // VGT_INDX_OFFSET the draw packet writes
  uint32_t count = 0;          // vertices or indices
  bool inline_data = false;    // Begin/UP: stream data at fetch 95 and indices in the ring
  uint32_t stride = 0;
  uint32_t vertex_bytes = 0;
  bool index32 = false;
};

// The bound vertex shader as the game link-patched it at one moment: D3DVertexShader_Bind rewrites the object in
// place and flips its variant flag, so a recorded draw keeps the code it was recorded with.
struct VsSnapshot {
  uint32_t object = 0;      // D3DVertexShader address
  bool variant1 = false;
  std::vector<uint8_t> code;  // the selected variant's microcode
};

VsSnapshot SnapshotVs(const guest::D3DDevice* device);

// Completes owned preparation jobs before the guest publishes a packet to the host.
void FinishPreparation(Packet& packet);

// Render thread, after executing a packet: hands its draws' stream and texture lists back to capture.
void Recycle(Packet& packet);

// Builds and submits a draw from `device`, a merged copy of a recorded and a live device struct; a.device names the
// Guest thread: submits the draw the runtime just built for these arguments.
void Capture(PPCContext& ctx, uint8_t* base, const DrawArgs& a);

// gpu_frame_stats: guest-thread capture time per section, in microseconds since the last call.
struct CaptureStats {
  uint64_t setup = 0, constants = 0, streams = 0, textures = 0, geometry = 0, finish = 0, submit = 0;
  uint64_t const_alloc = 0, const_float = 0, const_literals = 0, stream_copy = 0;  // parts of the sections above
  uint64_t stream_count = 0, stream_copies = 0, stream_bytes = 0, stream_samples = 0;
};
CaptureStats TakeCaptureStats();

// Guest thread: Begin*Vertices returned the guest buffer the title fills next;
// vertex_data and index_data are those guest addresses, 0 when unused.
void BeginInline(PPCContext& ctx, uint8_t* base, const DrawArgs& a, uint32_t vertex_data,
                 uint32_t index_data);

// Guest thread: submits a started inline draw whose End the title inlined.
void FinishPending();

// device whose shader cache and blend overrides apply. Inline (Begin/UP) draws are not replayed.
void Replay(PPCContext& ctx, uint8_t* base, const DrawArgs& a, const guest::D3DDevice* device, const VsSnapshot& vs);

// Guest thread: D3DDevice_GpuBeginShaderConstantF4 reserved `count` float4 registers of a stage from `start` at `data`
// in the ring, for the title to fill.
void NoteGpuConstants(uint32_t device, bool pixel, uint32_t start, uint32_t count, uint32_t data);

}  // namespace onevs100::render::draw
