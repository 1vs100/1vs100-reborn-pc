// Guest microcode to DXBC through the SDK's DxbcShaderTranslator, for D3D12 devices without shader model 6 (Xbox
// One). The translator binds like the XenosRecomp DXIL: b0-b4 root CBVs, t0/u0 shared memory, bindless textures in
// spaces 1-3, so its entries run on the same draw layout. Windows only.
#pragma once

#include <cstdint>
#include <span>

#include "render/gpu_records.h"
#include "render/gpu_shader_pack.h"

namespace onevs100::render::dxbc {

// Chosen once the device is known; every other function here requires it.
void SetActive(bool active);
bool Active();

// A guest shader with its microcode analyzed; lives for the process.
struct GuestShader;

// Guest thread. `ucode` is big-endian as it sits in guest memory; null when the microcode is unusable.
const GuestShader* Analyze(ShaderStage stage, uint64_t hash, std::span<const uint32_t> ucode);

// Per-draw inputs to the shader modifications (DxbcShaderTranslator::Modification).
struct DrawShaderState {
  uint32_t program_cntl = 0;      // SQ_PROGRAM_CNTL
  uint32_t context_misc = 0;      // SQ_CONTEXT_MISC
  uint32_t clip_cntl = 0;         // PA_CL_CLIP_CNTL
  uint32_t interpolator_cntl = 0; // SQ_INTERPOLATOR_CNTL
  uint32_t msaa_samples = 0;      // RB_SURFACE_INFO.msaa_samples
  bool point_list = false;
  bool coverage_depends_on_alpha = false;  // alpha test or alpha to coverage
};

struct DrawShaders {
  const ShaderEntry* vs = nullptr;
  const ShaderEntry* ps = nullptr;  // null when the draw has no pixel shader
};

// Guest thread: the translations for this draw state, translating on first use. vs is null on failure.
DrawShaders Translate(const GuestShader* vs, const GuestShader* ps, const DrawShaderState& state);

// Render thread: the translator's b0 (DxbcShaderTranslator::SystemConstants) from the native constants.
size_t SystemConstantsSize();
void WriteSystemConstants(const SystemConstants& native, const DxbcSystemExtras& extras, void* out);

}  // namespace onevs100::render::dxbc
