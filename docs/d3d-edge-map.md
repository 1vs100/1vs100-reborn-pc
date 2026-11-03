# Guest D3D entry points (retail 1vs100, v1.0.1149.0)

Addresses come from `~/Development/personal/x360/1vs100/ida-1vs100-retail/1vs100-retail.xex.i64`,
which carries names matched from the v1.0.808.0 debug build through
`signatures/1vs100-808-debug.sig` and `signatures/808-to-1149-matches.json`. Query it with
`ida_exec.sh` (see the README in that folder for starting and stopping the idalib driver).

The recompiled function for address `0xAAAAAAAA` is the symbol `sub_AAAAAAAA`. Generated code
defines these as weak aliases to `__imp__sub_AAAAAAAA` (`native/generated/default-v5/onevs100_pch.h`),
so a title-side `REX_HOOK_RAW(sub_AAAAAAAA)` in a TU that replaces the generated definition is
linked in place of the alias and therefore also intercepts direct guest calls, not only dispatched
ones.

## Draw submission

| Address | Size | Name |
|---|---|---|
| 0x822F29A8 | 1172 | `D3DDevice_BeginVertices` |
| 0x822F3440 | 1004 | `D3DDevice_DrawVertices` |
| 0x822F3830 | 1100 | `D3DDevice_DrawIndexedVertices` |
| 0x822F40F8 | 1500 | `D3DDevice_DrawIndexedTessellatedVertices` |
| 0x8230E4B0 | 340 | `D3D::XpsObject::DrawVertices` |
| 0x8230E608 | 440 | `D3D::XpsObject::DrawIndexedVertices` |
| 0x8230F408 | 372 | `D3DXps_DrawVertices` |
| 0x8230F580 | 476 | `D3DXps_DrawIndexedVertices` |
| 0x82305D48 | 88 | `D3D::PM4Draw` |

## Pipeline state

| Address | Name |
|---|---|
| 0x822E2E10 | `D3DDevice_SetStreamSource` |
| 0x822E2FB8 | `D3DDevice_SetIndices` |
| 0x822E2C88 | `D3DDevice_SetScissorRect` |
| 0x822E3948 | `D3DDevice_SetViewport` |
| 0x822E36E0 | `D3D::SetViewport` |
| 0x822EB3B8 | `D3DDevice_SetPixelShader` |
| 0x822EB5C0 | `D3DDevice_SetVertexShader` |
| 0x822EE6E0 | `D3DDevice_SetFVF` |
| 0x822EAD60 | `D3DDevice_BeginShaderConstantF4` |
| 0x822EAE80 | `D3DDevice_SetVertexShaderConstantFN` |
| 0x822EAF58 | `D3DDevice_SetPixelShaderConstantFN` |
| 0x822E3460 | `D3DDevice_SetVertexFetchConstant` |
| 0x822E3530 | `D3DDevice_SetTextureFetchConstant` |
| 0x822DFE90 | `D3DDevice_SetTexture` |
| 0x822E33E0 | `D3DDevice_SetBlendState` |
| 0x822E1528 | `D3DDevice_SetRenderState_ColorWriteEnable3` |
| 0x822E1B10 | `D3DDevice_SetRenderState_ViewportEnable` |
| 0x822E1C40 | `D3DDevice_SetRenderState_HighPrecisionBlendEnable` |
| 0x822E2118 | `D3DDevice_SetRenderState_HiZWriteEnable` |

## Resolve, clear, present

| Address | Name |
|---|---|
| 0x822E74D0 | `D3DDevice_Resolve` |
| 0x822E5C58 | `D3DDevice_ClearF` |
| 0x822DC8B8 | `D3DDevice_FlushHiZStencil` |
| 0x822F4E58 | `D3D::SimpleSwap` |
| 0x822F4ED8 | `D3D::ComplexSwap` |
| 0x822F50C0 | `D3DDevice_Swap` |
| 0x822F5720 | `D3DDevice_Present` |

## Ring and command buffers

| Address | Name |
|---|---|
| 0x822D9CC0 | `D3DDevice_SetRingBufferParameters` |
| 0x822D8330 | `D3D::CDevice::BeginRingAlloc` |
| 0x822D9970 | `D3D::CDevice::BeginRingBig` |
| 0x822D8FA8 | `D3D::CDevice::SetFence` |
| 0x822F9A88 | `D3DDevice_BeginCommandBuffer` |
| 0x822FBC20 | `D3DDevice_EndCommandBuffer` |
| 0x822FBFF8 | `D3DDevice_RunCommandBuffer` |
| 0x822FDBA8 | `D3D::InitializePresentationParameters` |
| 0x822FE0B0 | `D3D::InitializeHardwareDevice` |

## Microcode patching (shader edits the title makes to itself)

| Address | Name |
|---|---|
| 0x823045B0 | `D3D::SetPending_Shaders` |
| 0x823038D8 | `D3D::PatchVertexShaderToMatchVertexDeclaration` |
| 0x82304088 | `D3D::PatchVertexShaderToMatchPixelShader` |
| 0x82304470 | `D3D::DirectShaderPatch` |
| 0x822EBBC0 | `D3DDevice_SetShaderInstructionAllocation` |
| 0x822EBD40 | `D3DDevice_SetShaderGPRAllocation` |
