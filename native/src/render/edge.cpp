// Edge layer: the GPU-facing calls of the title's D3D runtime.
//
// The runtime is statically linked into the title, so its entry points are guest
// code. Each hook runs on the guest thread, turns the call into a renderer record
// and then calls the runtime so its own bookkeeping (ring cursor, dirty masks,
// pending fences) stays intact.

#include <cstdint>

#include <rex/ppc/context.h>
#include <rex/ppc/func.h>

#include "render/gpu.h"

namespace onevs100::render::edge {

namespace {

void (*g_vertex_shader_patch)(PPCContext& ctx, uint8_t* base) = nullptr;

}  // namespace

void SetVertexShaderPatcher(void (*patch)(PPCContext& ctx, uint8_t* base)) {
  g_vertex_shader_patch = patch;
}

void PatchVertexShader(PPCContext& ctx, uint8_t* base) {
  if (g_vertex_shader_patch != nullptr) {
    g_vertex_shader_patch(ctx, base);
  }
}

}  // namespace onevs100::render::edge
