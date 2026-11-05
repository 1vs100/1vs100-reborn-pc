# Native renderer on shader model 5.1 (Xbox One) - status

Xbox One D3D12 reports shader model 5.1, so DXIL is refused (`CreateGraphicsPipelineState` E_FAIL). On such devices the native renderer translates guest microcode with the SDK's `DxbcShaderTranslator` (Xenia lineage) instead of XenosRecomp. The draw layout already matches its bindings: b0-b4 root CBVs, t0/u0 shared memory, bindless textures in spaces 1-3, sampler table.

## Plan

- [x] M1 Build: the SDK translator sources compile into `onevs100_dxbc` (Windows); plume reports the device shader format (`RenderDeviceCapabilities::shaderFormat`, DXBC below shader model 6); `gpu_force_dxbc` takes the path on a DXIL device.
- [x] M2 Host shaders: blit, overlay, resolve, resize also embedded as SM 5.1 DXBC, compiled by vkd3d-shader (`tools/gpu/compile_shaders.py`). The resize macros that sent vkd3d into unbounded memory use are gone.
- [x] M3 Translation: `render/gpu_dxbc.cpp`, one `ShaderEntry` per (microcode, modification); bindings in `bindless_descriptor_index` order.
- [x] M4 Constants: b1 packed by the shader's float bitmap; b0 written as `DxbcShaderTranslator::SystemConstants`; no b5.
- [ ] M5 Geometry: point and rectangle lists are skipped on this path (logged once). Port `PipelineCache::CreateDxbcGeometryShader`. User clip planes go through the VS modification.
- [ ] M6 Console: 10.0.20.174 (Xbox One S) boots to the lobby with avatars; 10-14 fps in the lobby. 10.0.0.77 (Series S) renders the same with `gpu_force_dxbc`.

## Xbox One findings

- Upload chunks must be committed resources: the driver faults (`umd12ddi.dll`, in `CreateResource`) when D3D12MA places a second 32 MB upload buffer in the same heap. Seen as a GPU stall after the first draw, then the process is removed.
- UWP titles get D3D11 feature level 10 only, also as type Game; SDL3-uwp's D3D11 renderer needs 10_0/10_1 in its list and a nearest-sampling fallback for pixel-art scaling (fork patch).

## Open

- Frame rate on the One S.
- Signed texture fetches sample the unsigned view (native views ignore `is_signed`).
- Gamma render targets are not converted (matches the DXIL path).
- `onevs100_dxbc` compiles SDK sources that also live in the xenos plugin, so with `native_renderer = false` on Windows the plugin's copies of those cvars fail to register and keep their defaults.
