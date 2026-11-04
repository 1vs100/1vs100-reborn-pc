# Guest D3DDevice layout of 1 vs 100 (retail v1.0.1149.0)

Status of `native/src/render/guest/d3d_device.h`: the struct in that header was copied
from CrashCourse2Recomp and its `static_assert(offsetof(...))` lines record offsets that
CC2 verified against CC2's own title. 1 vs 100 links a different (newer) XDK, so every
offset in that header is a hypothesis here. This document records what 1 vs 100's own
code actually does.

## 1. Method and evidence quality

The 1 vs 100 retail XEX was loaded in IDA with names carried over from the v1.0.808.0
debug build (callgraph and order matching), and with the Hex-Rays decompiler available.
Every offset below was read out of the retail disassembly as a literal displacement from
the device pointer, inside a function whose identity is either a recovered name or, for
unnamed functions, established from behaviour and call sites (stated in the evidence
column).

All evidence quotes the instruction address of the retail XEX:

    cd ~/Development/personal/x360/1vs100/ida-1vs100-retail
    ./ida_exec.sh    # idalib REPL, then idc.GetDisasm / ida_hexrays.decompile

Evidence quality tiers used in the tables:

* direct: a literal displacement in a recovered (named) runtime function, so both the
  field identity and the offset are established.
* strong: a literal displacement in an unnamed function whose role is pinned down by its
  call sites, its arguments or its neighbours in the library.
* inferred: no direct displacement found; the offset follows arithmetically from a
  verified neighbour plus the XDK struct definition (`/home/e99/.../xdk/include/xbox`,
  d3d9.h / d3d9gpu.h, version 21256). Treated as likely but not proven.

The shipped XDK headers are version 21256 and the title's XDK is unknown, so the headers
were used only for member order and array bounds, never as proof of an offset. Where the
two disagree (the per-sampler shadow arrays, and the end of the struct) the disassembly
wins.

## 2. Shift groups

The layout is not shifted uniformly. There are four regions.

Group 0, unchanged: everything from `m_Pending` (0x00) through `m_PointPacket` (ends
0x2A6C) is byte identical, including the whole of `m_Constants`, `m_ClipPlanes` and all
eight register packets. Every `static_assert` in that span holds as written.

Group 1, private state region (CC2 0x2A70 to 0x3140): reorganised, no single shift.
The CC2 field set (two 26-entry sampler arrays plus a handful of stand-alone render-state
bytes) does not exist here. This build has three 26-entry sampler arrays (min mip, max
mip, z filter), and of CC2's stand-alone render-state fields only the scissor enable and
the four colour-write enables were located; z enable, stencil enable and the point size
family are gone. The individual shifts that were measured are listed in the table:
-0x80 (thread owner, writeback page, fence counter, suspend flag), -0x84 (primary write
index), -0xF8 (vertex declaration), -0x100 (scissor enable, colour write enables).

Group 2, shift -0xC8: `index_buffer` through `vertex_shader`
(CC2 0x320C..0x3310 becomes 0x3144..0x3248), and `swap_writeback_index`
(CC2 0x5614 becomes 0x554C). One shift explains all of these.

The two measured shifts are consistent with each other: in CC2's model the gap from
`scissor_test_enable` (0x3000) to `index_buffer` (0x320C) is 0x20C, in this build the gap
from 0x2F00 to 0x3144 is 0x244, so 0x38 more shadow state sits between them. 0x100 - 0x38
= 0xC8, which is exactly the shift of group 2. At least 0x1A of the extra 0x38 is the
third 26-entry sampler array, which CC2's model does not have; the remainder is other
added shadow state and alignment plus the fields listed in 3.4 and 3.7.

Group 1 also breaks CC2's assumption that the region is a set of small fixed fields: three
26-byte per-sampler arrays now sit between `color_write_enable` and `index_buffer`, at
0x2F5E (min mip), 0x2F78 (max mip) and 0x2F92 (packed z filter).

Group 3, shift -0xC4: `suspend_lock` (CC2 0x3BF4 becomes 0x3B30). Not the same shift as
group 2.

Struct size: CC2's model stops at 0x5618. This build allocates 0x5F00 bytes for the
device (`Direct3D_CreateDevice` 0x822DCE18 at 0x822DCE34: `li r3, 0x5F00`), so the
structure is at least 0x5E84 bytes and the `sizeof(D3DDevice) == 0x5618` assert is wrong
here. The `0x5618` value is not a size in this build at all.

## 3. D3DDevice field table

Offset columns are CC2's asserted offset and the offset used by this build. Instruction
addresses are in the retail XEX.

### 3.1 Device header (group 0, all unchanged)

| member path | CC2 | 1 vs 100 | verdict | evidence |
| --- | --- | --- | --- | --- |
| m_Pending | 0x00 | 0x00 | same | inferred from every m_Mask access below |
| m_Pending.m_Mask | 0x00 | 0x00 | same | D3DDevice_SetStreamSource 0x822E2E74 `ld r11,0x18(r3)` (m_Mask[3]); D3D::SetViewport 0x822E38E8 `ld r11,0x10(r31)` (m_Mask[2]) |
| m_Predicated_PendingMask2 | 0x28 | 0x28 | same | D3DDevice_SetRenderState_HiZWriteEnable 0x822E2140 `ld r11,0x28(r3)`, 0x822E2158 `std r11,0x28(r3)` |
| m_pRing | 0x30 | 0x30 | same | SetPending_FetchConstants 0x82303644 `lwz r4,0x30(r3)`; D3DDevice_DrawVertices 0x822F3684 `lwz r3,0x30(r31)`, 0x822F3800 `stw r11,0x30(r31)` |
| m_pRingLimit | 0x34 | 0x34 | same | SetPending_FetchConstants 0x82303658 `lwz r8,0x34(r27)` |
| m_pRingGuarantee | 0x38 | 0x38 | same | SetPending_FetchConstants 0x823036CC `lwz r11,0x38(r27)` |
| m_ReferenceCount | 0x3C | 0x3C | same | D3DDevice_Release 0x822DC668 `lwz r11,0x3C(r3)`, 0x822DC694 `stw r3,0x3C(r31)` |
| m_SetRenderStateCall | 0x40 | 0x40 | same | sub_822FD940 0x822FD998 `lwz r11,0x40(r9)` inside a loop bounded by 0x194, which is 101 four-byte entries = D3DRS_MAX/4 |
| m_SetSamplerStateCall | 0x1D4 | 0x1D4 | same | sub_822FD940 0x822FDA00 `lwz r11,0x1D4(r9)`; D3DStateBlock_Apply 0x822E6A10 `lwz r11,0x1D4(r11)` |
| m_GetRenderStateCall | 0x224 | 0x224 | same | sub_822E61D8 0x822E61F0 `lwz r11,0x224(r11)` then `mtctr r11; bctrl` |
| m_GetSamplerStateCall | 0x3B8 | 0x3B8 | same | sub_822E6230 0x822E6248 `lwz r11,0x3B8(r11)` then `mtctr r11; bctrl` |
| m_Constants | 0x480 | 0x480 | same | D3DDevice_SetVertexFetchConstant 0x822E3480 `addi r10,r29,0x30` with 0x822E3488 `mulli r10,r10,0x18` (0x30*0x18 = 0x480); sub_822FD940 0x822FD9BC `addi r27,r31,0x480`; D3DDevice_SetVertexShader 0x822EB6C0 `addi r28,r30,0x480` |

### 3.2 D3DConstants (group 0, all unchanged)

`GPU_FETCH_CONSTANTS` is 32 entries of 0x18, ALU constants are 512 vectors (0x2000),
flow constants are 24 dwords, and m_ClipPlanes was verified at 0x2820, so the whole block
is pinned at both ends.

| member path | CC2 | 1 vs 100 | verdict | evidence |
| --- | --- | --- | --- | --- |
| m_Constants.Fetch | 0x480 | 0x480 | same | D3DDevice_SetStreamSource 0x822E2E70 `stw r6,0x6F4(r9)` with r9 = pDevice + (17-stream)*8, which is Fetch[26].dword[1] (0x480 + 26*0x18 + 4 = 0x6F4) |
| Fetch slot 0..25 are the sampler fetch constants | 0x480 | 0x480 | same | D3DDevice_SetTexture 0x822DFEA8 `addi r11,r4,0x30`, 0x822DFEB0 `mulli r11,r11,0x18`, 0x822DFEC0 `add r11,r11,r3` (0x18*(0x30+sampler)) |
| Fetch slot 26..31 are the vertex fetch constants | 0x6F0 | 0x6F0 | same | D3DDevice_SetVertexFetchConstant 0x822E346C `subfic r11,r4,0x5F`, 0x822E3478 `divwu r29,r11,3`, 0x822E3480 `addi r10,r29,0x30`, 0x822E3488 `mulli r10,r10,0x18` |
| m_Constants.VertexShaderF | 0x780 | 0x780 | same | D3DDevice_SetVertexShaderConstantFN 0x822EAE80 `addi r10,r4,0x78` then `slwi r10,r10,4` (0x10*(0x78+index)) |
| m_Constants.PixelShaderF | 0x1780 | 0x1780 | same | D3DDevice_SetPixelShaderConstantFN 0x822EAF58 `addi r10,r4,0x178` then `slwi r10,r10,4` |
| m_Constants.VertexShaderB | 0x2780 | 0x2780 | inferred | no direct displacement; follows from PixelShaderF at 0x1780 plus 0x1000 bytes of ALU vectors, and from m_ClipPlanes at 0x2820 (below) minus the 0xA0-byte flow block |
| m_Constants.PixelShaderB | 0x2790 | 0x2790 | inferred | as above |
| m_Constants.VertexShaderI | 0x27A0 | 0x27A0 | inferred | as above |
| m_Constants.PixelShaderI | 0x27E0 | 0x27E0 | inferred | as above |
| m_ClipPlanes | 0x2820 | 0x2820 | same | SetPending_ClipPlanes 0x823034D0 `addi r31,r29,0x2820` |

### 3.3 Register packets (group 0, all unchanged)

Each packet base was verified with a literal displacement, and at least one member of
each packet was verified at its CC2 offset. The remaining members of a packet are
inferred from the verified base plus the d3d9gpu.h definitions, which are the same
struct definitions CC2's model used (all packet sizes are fixed by the GPU register
order, not by XDK version).

| member path | CC2 | 1 vs 100 | verdict | evidence |
| --- | --- | --- | --- | --- |
| m_DestinationPacket (all members) | 0x2880 | 0x2880 | same | 0x2880 read in SetPending_Predicated 0x823032EC `lwz r9,0x2880(r31)`; Color0Info 0x2884 in SetPending_Shaders 0x823046D0 `lwz r11,0x2884(r31)` and HighPrecisionBlendEnable 0x822E1C50 |
| m_WindowPacket (all members) | 0x28C0 | 0x28C0 | same | D3DDevice_SetScissorRect 0x822E2D44 `lwz r11,0x28C4(r3)`, 0x822E2D48 `lwz r10,0x28C8(r3)` (WindowScissorTL/BR) |
| m_ValuesPacket (all members) | 0x28CC | 0x28CC | same | D3D::SetViewport 0x822E38B8 `stfs f0,0x2918(r31)` (VportZScale), 0x822E38C8 `stfs f13,0x2908(r31)` (VportXScale), 0x822E38E4 `stfs f0,0x2914(r31)` (VportYOffset); ColorMask 0x28DC in SetRenderState_ColorWriteEnable3 0x822E1530 |
| m_ProgramPacket (all members) | 0x2920 | 0x2920 | same | SetPending_Shaders 0x82304B2C `stw r9,0x2920(r31)` (ProgramControl), 0x82304B1C `stw r9,0x2924(r31)` (ContextMisc), 0x82304800 `stw r10,0x2928(r31)` (InterpolatorControl) |
| m_ControlPacket (all members) | 0x2934 | 0x2934 | same | sub_822E0FE8 0x822E0FE8 `lwz r11,0x2934(r3)`, `extrwi r3,r11,1,29` (DepthControl); SetRenderState_ViewportEnable 0x822E1B2C `lwz r10,0x2944(r3)` (ClipControl), 0x822E1B20 `stw r11,0x294C(r3)` (VteControl); SetPending_Shaders 0x823046C4 `stw r11,0x2954(r31)` (EdramModeControl) |
| m_TessellatorPacket (all members) | 0x2964 | 0x2964 | same | sub_822E1638 0x822E166C `stw ...,0x2964(r3)` (PointSize); D3DDevice_DrawIndexedTessellatedVertices 0x822F4134 `lwz ...,0x2978(r31)` (HosControl), 0x822F4230 `0x2994(r31)` (GroupVect0Control) |
| m_MiscPacket (all members) | 0x29B8 | 0x29B8 | same | D3DDevice_Resolve 0x822E7850 `stw r30,0x2A18(r31)` (CopyControl), 0x822E7834 `0x2A1C` (CopyDestBase), 0x822E782C `0x2A24` (CopyDestInfo); sub_822E6E60 0x822E6F58 `0x2A28` (HiClear), 0x822E6F38 `0x2A2C` (DepthClear); D3D::SetColorClear 0x822E74B4 `0x2A30` (ColorClear), 0x822E74B8 `0x2A34` (ColorClearLo) |
| m_PointPacket (all members) | 0x2A50 | 0x2A50 | same | sub_822E1368 0x822E13CC `0x2A50(r31)` (PolyOffsetFrontScale), 0x822E138C `0x2A54` (PolyOffsetFrontOffset), 0x822E13D4 `0x2A58`, 0x822E13B4 `0x2A5C`; getter sub_822E14C8 0x822E14C8 `lfs f0,0x2A54(r3)` |

### 3.4 Private state region (group 1, reorganised)

| member path | CC2 | 1 vs 100 | verdict | evidence |
| --- | --- | --- | --- | --- |
| m_MaxAnisotropy[26] | 0x2A70 | unknown | unknown | see section 7. This build's sampler shadows are the three arrays below, and D3DDevice_SetTexture no longer clamps the fetch constant's AnisoFilter field, so no max-anisotropy shadow is read by anything found so far |
| m_ZFilter (per-sampler packed z filter) | 0x2A8A | 0x2F92 | shifted (+0x508, into the per-sampler shadow block; the byte contents changed: min filter z, mag filter z and the separate-z-enable bit are packed into one byte per sampler) | D3DDevice_SetSamplerState_MinFilterZ 0x822E23D8 `stb r6,0x2F92(r11)`; MagFilterZ 0x822E2578; SeparateZFilterEnable 0x822E2678 |
| thread_owner | 0x2B08 | 0x2A88 | shifted (-0x80) | D3DDevice_AcquireThreadOwnership 0x822DC740 `lwz r11,0x2A88(r3)`, 0x822DC758 `stw r3,0x2A88(r31)`; D3D::CDevice::ThreadOwnsDevice 0x822D7D58 `0x2A88` |
| writeback_page | 0x2B10 | 0x2A90 | shifted (-0x80) | D3D::CDevice::SetFence 0x822D8FC0 `lwz r10,0x2A90(r3)`, 0x822D9074 `stw r8,0(r10)`; D3DDevice_Swap 0x822F51E0 `lwz r10,0x2A90(r31)`; CBlocker::Check 0x822DC5B0 `*(v2+10896)` (10896 = 0x2A90) |
| fence_next | 0x2B1C | 0x2A9C | shifted (-0x80) | D3D::CDevice::SetFence 0x822D8FD0 `lwz r8,0x2A9C(r11)`, 0x822D9080 `addi r10,r8,2`, 0x822D9084 `stw r10,0x2A9C(r11)` (the value advances by 2, as CC2 documented) |
| suspend_flags | 0x2B3E | 0x2ABE | shifted (-0x80), but it is now one of a set of flag bits: bit 0x02 = suspended | Suspend 0x822DC798 0x822DC7F4 `lbz r10,0x2ABE(r31)`, 0x822DC804 `ori r10,r10,2`; Resume 0x822DC830 0x822DC884 `lbz r11,0x2ABE(r31)`, 0x822DC890 `andi. r11,r11,0xFD` |
| primary_write_index | 0x2B50 | 0x2ACC | shifted (-0x84) | sub_822D8B98 (add-calls-to-primary-buffer) 0x822D8D88 `stw r11,0x2ACC(r29)`, then 0x822D8D90 `lis r10,0x7FC8`, 0x822D8D94 `stw r11,0x714(r10)` which is CP_RB_WPTR at 0x7FC80714 |
| queued_chunks | 0x2B90 | unknown | unknown | this build keeps the outstanding indirect-buffer list in a CRingAllocList at 0x3500 with a spin lock at 0x2B08 (D3D::CDevice::QueueIndirectBuffers 0x822D91A0 `addi r3,r3,0x3500`, 0x822D91F0 `addi r27,r31,0x2B08`, 0x822D91AC `lwz r11,0x2B04(r31)`). No single counter matching CC2's description was identified |
| vertex_declaration | 0x2FD0 | 0x2ED8 | shifted (-0xF8) | D3DDevice_SetFVF 0x822EE708 `stw r4,0x2ED8(r31)`; D3DDevice_GetVertexDeclaration 0x822EB808 `lwz r31,0x2ED8(r3)` |
| scissor_test_enable | 0x3000 | 0x2F00 | shifted (-0x100) | D3DDevice_SetScissorRect 0x822E2CF0 `lwz r6,0x2F00(r3)`; sub_822E55A8 0x822E5698 |
| color_write_enable[4] | 0x3004 | 0x2F04 | shifted (-0x100) | D3DDevice_SetRenderState_ColorWriteEnable3 0x822E1538 `stw r4,0x2F04(r3)`; sub_822E3A78 / sub_822E40A0 read 0x2F04, 0x2F08, 0x2F0C, 0x2F10 (for example 0x822E3CF4, 0x822E3D30, 0x822E3D5C, 0x822E3D88) |
| z_enable | 0x3014 | unknown | unknown | see section 7 |
| stencil_enable | 0x3018 | unknown | unknown | see section 7 |
| point_sprite_enable | 0x301C | unknown | unknown | see section 7 |
| point_size | 0x3024 | unknown | unknown | see section 7 |
| point_size_min | 0x3028 | unknown | unknown | see section 7 |
| point_size_max | 0x302C | unknown | unknown | see section 7 |
| max_mip_level[26] | 0x3044 | 0x2F78 | shifted (-0xCC) | D3DDevice_SetTexture 0x822DFF68 `lbz r9,0x2F78(r9)` where r9 = pDevice + sampler; compared against the fetch constant's MaxMipLevel bits 6..9 of texture+0x2C |
| min_mip_level[26] | 0x305E | 0x2F5E | shifted (-0x100) | D3DDevice_SetSamplerState_MinMipLevel 0x822E28CC `stb r5,0x2F5E(r11)`; D3DDevice_SetTexture 0x822DFF24 `lbz r8,0x2F5E(r9)` (MinMipLevel bits 2..5 of texture+0x2C) |
| high_precision_blend_enable | 0x3078 | 0x2FAC | shifted (-0xCC) | D3DDevice_SetRenderState_HighPrecisionBlendEnable 0x822E1C44 `stw r4,0x2FAC(r3)`; sub_822E40A0 reads 0x2FAC, 0x2FB0, 0x2FB4, 0x2FB8 (for example 0x822E411C, 0x822E41C0, 0x822E4264, 0x822E4308) |
| (no CC2 counterpart) FVF | n/a | 0x3140 | new field | D3DDevice_SetFVF 0x822EE6F0 `stw r4,0x3140(r3)`; UpdateStateBlock 0x822E65A4 |

Note on the two name-like fields the decompiler prints as `m_MaxAnisotropy[24]` and
`m_ZFilter[18]`: those are artifacts of the IDA type import (the 21256 header type ends at
`m_ZFilter`), not real member accesses. In this build 0x2A9C and 0x2AA0 are the fence
counter and its companion dirty-mask word; see the fence_next row.

### 3.5 Render target, stream, texture, viewport block (group 2, shift -0xC8)

| member path | CC2 | 1 vs 100 | verdict | evidence |
| --- | --- | --- | --- | --- |
| index_buffer | 0x320C | 0x3144 | shifted (-0xC8) | D3DDevice_SetIndices 0x822E2FC4 `lwz r30,0x3144(r3)`, 0x822E303C `stw r29,0x3144(r31)`; D3DDevice_GetIndices 0x822E305C |
| render_targets[4] | 0x3210 | 0x3148 | shifted (-0xC8) | D3DDevice_GetRenderTarget 0x822E3098 `addi r11,r4,0xC52` then `slwi r11,r11,2` (0x3148 + 4*index); D3D::SetViewport 0x822E36F8 `lwz r10,0x3148(r3)` |
| depth_stencil | 0x3220 | 0x3158 | shifted (-0xC8) | D3DDevice_GetDepthStencilSurface 0x822E30E0 `lwz r31,0x3158(r3)`; D3D::SetViewport 0x822E371C `lwz r9,0x3158(r3)` |
| stream_sources[16] | 0x3224 | 0x315C | shifted (-0xC8) | D3DDevice_SetStreamSource 0x822E2E80 `addi r11,r29,0xC57`, 0x822E2E84 `slwi r27,r11,2` (0x315C + 4*stream), 0x822E2EFC `stwx r30,r27,r31`; D3DDevice_GetStreamSource 0x822E2F3C |
| stream_stride_dwords[16] | 0x3268 | 0x31A0 | shifted (-0xC8) | D3DDevice_SetStreamSource 0x822E2F08 `stb r9,0x31A0(r11)` where r11 = pDevice + stream; D3DDevice_GetStreamSource 0x822E2FA0 `lbz r11,0x31A0(r11)` then `rotlwi r11,r11,2` (stride shift back) |
| textures[26] | 0x3278 | 0x31B0 | shifted (-0xC8) | D3DDevice_SetTexture 0x822DFE9C `addi r11,r4,0xC6C`, 0x822DFEA4 `slwi r29,r11,2` (0x31B0 + 4*sampler), 0x822DFFA4 `stwx r5,r29,r31`; D3DDevice_GetTexture 0x822E0034 |
| viewport (X, Y, Width, Height, MinZ, MaxZ) | 0x32E0 | 0x3218 | shifted (-0xC8) | D3D::SetViewport 0x822E3860 `stfs f31,0x3218(r31)`, 0x822E3868 `0x321C`, 0x822E38A8 `0x3220`, 0x822E3894 `0x3224`, 0x822E3888 `0x3228`, 0x822E3890 `0x322C`; D3DDevice_GetViewport 0x822E2DA8 reads 0x3218..0x322C |
| scissor_rect (left, top, right, bottom) | 0x32FC | 0x3234 | shifted (-0xC8) | D3DDevice_SetScissorRect 0x822E2CF4 `stw r11,0x3234(r3)`, 0x822E2CFC `0x3238`, 0x822E2D04 `0x323C`, 0x822E2D08 `0x3240`; D3D::SetViewport 0x822E389C `addi r4,r31,0x3234` (passed to SetScissorRect) |
| pixel_shader | 0x330C | 0x3244 | shifted (-0xC8) | D3DDevice_SetPixelShader 0x822EB43C `stw r29,0x3244(r30)`; D3DDevice_GetPixelShader |
| vertex_shader | 0x3310 | 0x3248 | shifted (-0xC8) | D3DDevice_SetVertexShader 0x822EB660 `stw r29,0x3248(r30)`; D3DDevice_GetVertexShader |
| (no CC2 counterpart) viewport trailing dword | n/a | 0x3230 | new field | D3D::SetViewport 0x822E3898 `stw r11,0x3230(r31)` with r11 = 0 |

### 3.6 Tail of the struct

| member path | CC2 | 1 vs 100 | verdict | evidence |
| --- | --- | --- | --- | --- |
| suspend_lock | 0x3BF4 | 0x3B30 | shifted (-0xC4) | D3D::InitializeHardwareDevice 0x822FE0C8 `addi r3,r3,0x3B30` then `bl RtlInitializeCriticalSection`; Suspend 0x822DC7E4 `addi r30,r31,0x3B30`, 0x822DC7EC `bl RtlEnterCriticalSection`. A second critical section sits at 0x3B4C (0x822FE0D4) |
| swap_writeback_index | 0x5614 | 0x554C | shifted (-0xC8) | D3DDevice_Swap 0x822F51CC `lwz r11,0x554C(r31)`, 0x822F51DC `clrlwi r11,r11,29` (slot = value & 7), 0x822F5204 `stw ...` back via 0x822F52B8 writes; the slot indexes the writeback page at 0x2A90 (0x822F51E0) |
| sizeof(D3DDevice) | 0x5618 | 0x5F00 allocated | different | Direct3D_CreateDevice 0x822DCE18, 0x822DCE34 `li r3,0x5F00`, 0x822DCE44 `bl D3D::MemAllocAligned`; the device flags word is stored at 0x5E80 (0x822DCEC8 `stw r30,0x5E80(r31)`), so the struct is at least 0x5E84 bytes |

### 3.7 Fields with no CC2 counterpart

These are present in this build and absent from CC2's model. The port may need them.

| field | offset | evidence |
| --- | --- | --- |
| FVF | 0x3140 | D3DDevice_SetFVF 0x822EE6F0 |
| saved ring pointer for inline draws | 0x3554 | D3DDevice_BeginVertices 0x822F2E28 `stw r11,0x3554(r31)`; EndVertices 0x822F2988 `lwz r11,0x3554(r3)`; 0x3558, 0x355C, 0x3560, 0x3564 hold the rest of the saved state (sub_822F2E88 0x822F3360..0x822F3370) |
| buffer resource cursor | 0x35A4 / 0x35A8 | D3DDevice_SetStreamSource 0x822E2EB8 `lwz r11,0x35A8(r31)`, 0x822E2EEC / 0x822E2EF4; made by D3D::MakeBufferResourceSpace |
| spin lock for the ring alloc list | 0x2B08 | D3D::CDevice::QueueIndirectBuffers 0x822D91F0 `addi r27,r31,0x2B08` then `bl KfAcquireSpinLock` |
| ring alloc list | 0x3500 | D3D::CDevice::QueueIndirectBuffers 0x822D91A0 `addi r3,r3,0x3500` |
| vfetch stride shadow | 0x2EE8 / 0x2EF0 | D3DDevice_SetStreamSource 0x822E2F10 `lbz r11,0x2EE8(r11)`; D3D::DirectShaderPatch 0x823045A0 `std r11,0x2EE8(r30)`, 0x823045A4 `std r10,0x2EF0(r30)` |
| ring / fence bookkeeping | 0x3460, 0x3464, 0x3468, 0x346C, 0x3B00..0x3B2C | D3D::CDevice::SetFence 0x822D8FD8 `stw r7,0x346C(r11)`, 0x822D8FE0 `stw r5,0x3468(r11)`; D3D::CDevice::BlockOnPrimaryRange 0x822D781C `0x3B04` |
| device flags word | 0x5E80 | Direct3D_CreateDevice 0x822DCEC8; D3DDevice_Swap 0x822F52B8 |

## 4. Function address map

CC2 addresses are the ones the ported sources and `docs/reference/*.hooks.txt` still
reference. "1 vs 100" is the retail address of the equivalent function.

| CC2 | name | 1 vs 100 | evidence |
| --- | --- | --- | --- |
| 0x825440B0 | D3DDevice_DrawVertices | 0x822F3440 | recovered name D3DDevice_DrawVertices |
| 0x825444C8 | D3DDevice_DrawIndexedVertices | 0x822F3830 | recovered name D3DDevice_DrawIndexedVertices |
| 0x825435C8 | D3DDevice_BeginVertices | 0x822F29A8 | recovered name D3DDevice_BeginVertices; also called from the public wrapper sub_825A5440 at 0x825A54A0 |
| 0x82543AD0 | D3DDevice_BeginIndexedVertices | 0x822F2E88 | unnamed; nine arguments (device, primitive type, base vertex, num vertices, index count, index format, stride, ppIndexData, ppVertexData), stores the two output pointers at 0x822F3374 and 0x822F3378 and saves the inline-draw state at 0x3554..0x3564. No other function in the module has this shape |
| 0x825435B8 | D3DDevice_EndIndexedVertices | 0x822F2988 | the only function that restores m_pRing from the saved slot (0x822F2988 `lwz r11,0x3554(r3)`; `stw r11,0x30(r3)`). Both EndVertices and EndIndexedVertices share this one body; the four call sites are the thin public wrappers sub_82456F00, sub_824576E8, sub_825A5440, sub_825A54E8 |
| 0x82543A88 | D3DDevice_DrawVerticesUP | 0x822F2E40 | unnamed; calls D3DDevice_BeginVertices, memcpy of count*stride, then restores m_pRing from 0x3554 |
| 0x82544010 | D3DDevice_DrawIndexedVerticesUP | 0x822F33A0 | unnamed; calls sub_822F2E88 then two memcpy calls (0x822F3400, 0x822F342C) then restores m_pRing from 0x3554 |
| 0x82547510 | D3DDevice_GpuSetBlendControl | 0x822F7288 | recovered name D3DDevice_GpuSetBlendControl |
| 0x82538150 | D3DDevice_CreateVertexBuffer | 0x822DAB10 | recovered name; public wrapper `?CreateVertexBuffer@D3DDevice@@...` at 0x82553C98 calls it at 0x82553CB8 |
| 0x82538218 | D3DVertexBuffer_Unlock | 0x822DAC88 | unnamed, 4 instructions: `lwz r11,0x18(r3)`, `li r5,0`, `clrrwi r4,r11,2`, `b D3D::UnlockResource`. Sits between CreateVertexBuffer and CreateConstantBuffer, and its public wrapper 0x8227FB30 is the twin of the vertex-buffer lock wrapper 0x8227FAF8 |
| 0x82538228 | D3DDevice_CreateIndexBuffer | 0x822DACA8 | recovered name; public wrapper `?CreateIndexBuffer@D3DDevice@@...` at 0x825B1668 calls it at 0x825B168C |
| 0x82539330 | D3DVertexBuffer_Lock | 0x822DBD20 | unnamed; calls sub_822DB958 with block mode 10 and the vertex fetch base/size (0x822DBD20 `a1[6] & 0xFFFFFFFC`, `a1[7] & 0x3FFFFFC`); public wrapper 0x8227FAF8 stores its result into the caller's ppbData |
| 0x82539380 | D3DIndexBuffer_Lock | 0x822DBE50 | unnamed; calls sub_822DB958 with block mode 12 and `a1[6]` / `a1[7]` unmasked (index buffer Address/Size); public wrapper 0x825B1788 |
| 0x8253A700 | D3D::LockSurface | 0x822DEAF8 | recovered name `?LockSurface@D3D@@YAXPAUD3DBaseTexture@@...` |
| 0x8253A7D8 | D3D::Lock2DSurface | 0x822DEBD0 | recovered name `?Lock2DSurface@D3D@@YAXPAUD3DBaseTexture@@...` |
| 0x8253B0E0 | D3DDevice_CreateTexture | 0x822DFAF0 | recovered name D3DDevice_CreateTexture |
| 0x8276E158 | XGOffsetBaseTextureAddress | 0x824773F8 | recovered name XGOffsetBaseTextureAddress |
| 0x8276E1C0 | XGOffsetResourceAddress | 0x82477460 | recovered name XGOffsetResourceAddress |
| 0x8276E0E8 | XGSetTextureHeader | 0x82476D20 | recovered name XGSetTextureHeader |
| 0x82545C80 | ALU ring store | 0x82303788 (probable) | no recovered name; see the note below |
| 0x82545DD0 | D3D::PatchVertexShaderToMatchVertexDeclaration | 0x823038D8 | recovered name `?PatchVertexShaderToMatchVertexDeclaration@D3D@@YAXPAVCVertexShader@1@PAKPBVCVertexDeclaration@1@PBEK@Z` |
| 0x828D6A70 | memcpy | 0x824AF050 | recovered name `memcpy`; the D3D module's own memcpy calls resolve here (for example 0x823043F0 in SetLiteralShaderConstants) |

Notes.

* 0x82545C80 is the only entry without a solid match. The ported sources do not
  reference it (only 0x82545DD0 appears, in `native/src/render/gpu_draw.cpp`). The best
  candidate is `D3D::SetPending_AluConstants` at 0x82303788: it is a ring writer that
  emits four ALU constant vectors per iteration (0x82303890 to 0x823038B8), and its
  address is exactly 0x150 below PatchVertexShaderToMatchVertexDeclaration, which is the
  same distance as in CC2 (0x82545DD0 - 0x82545C80 = 0x150). Treated as probable, not
  proven, because function sizes in this XDK differ elsewhere (PatchVertexShader to
  SetPending_Shaders is 0xC48 in CC2 but 0xCD8 here).
* The port also hooks D3DResource_Release. CC2 0x82538DD0 corresponds to 0x822DBCA0
  (recovered name D3DResource_Release).
* DrawVerticesUP and DrawIndexedVerticesUP are not the "run Begin, copy, End inline"
  functions the CC2 note describes in this build: here they are ordinary functions that
  call BeginVertices / BeginIndexedVertices and memcpy. The port's capture therefore has
  to intercept the pair, not a fused body.

## 5. Resource structures

All four resource headers match CC2 exactly. Each size is confirmed by the allocator call
inside the matching Create function, and each field by a literal displacement.

### D3DResource, 0x18 bytes (unchanged)

| field | offset | evidence |
| --- | --- | --- |
| Common | 0x00 | D3DDevice_CreateVertexBuffer 0x822DABA0 `stw r30,0(r31)`; D3DResource_IsSet reads `Common & 0xF` as the resource type |
| ReferenceCount | 0x04 | D3DDevice_CreateVertexBuffer 0x822DABB8 `stw r8,4(r31)` with r8 = 1; D3DDevice_CreateSurface 0x822DF064 |
| Fence | 0x08 | sub_822DB958 (the shared lock body) 0x822DBA54 `lwz r31,8(r10)` loads the fence of the resource and passes it to BlockOnFence at 0x822DBAA4; D3DDevice_SetStreamSource 0x822E2EA0 `stw r11,8(r28)` writes the current fence into the replaced resource |
| ReadFence | 0x0C | sub_822DB958 reads 0xC as the read fence (`v39[3]` in the decompilation) |
| Identifier | 0x10 | inferred: sits between the verified ReadFence (0xC) and BaseFlush (0x14). No setter or getter for it was identified in this build; D3D::GetExportIdentifier 0x822DA918 does not read it (it derives the address from Common's type nibble instead). See section 7 |
| BaseFlush | 0x14 | D3DDevice_CreateVertexBuffer 0x822DABC0 `stw r10,0x14(r31)` with r10 = -1; D3DDevice_CreateSurface 0x822DF078; D3D::SetTextureHeader 0x822DEE7C |

### D3DBaseTexture, 0x34 bytes (unchanged)

Allocated with size 0x34 by D3DDevice_CreateTexture 0x822DFB0C (`li r3,0x34`).

| field | offset | evidence |
| --- | --- | --- |
| resource | 0x00 | D3DDevice_CreateTexture 0x822DFBFC..0x822DFC00 writes 0x20 and 0x30 of the fetch constant |
| MipFlush | 0x18 | D3D::SetTextureHeader 0x822DEE80 `stw r9,0x18(r11)` with r9 = -1 |
| Format (GPUTEXTURE_FETCH_CONSTANT, 6 dwords) | 0x1C | D3D::SetTextureHeader 0x822DEF3C `lwz r6,0x1C(r11)`; D3DDevice_SetTextureFetchConstant 0x822E3548 `addi r4,r5,0x1C` then 0x822E3550 `li r5,0x18` and memcpy |
| Format.dword[1] (base address) | 0x20 | D3D::LockSurface 0x822DEB54 `lwz r11,0x20(r30)`, `clrlwi r3,r11,26` extracts DataFormat; D3DDevice_CreateTexture 0x822DFBEC |
| Format.dword[4] (mip levels, filters) | 0x2C | D3DDevice_SetTexture 0x822DFECC `lwz r26,0x2C(r5)` |
| Format.dword[5] (mip base address) | 0x30 | D3D::LockSurface 0x822DEB90 `lwz r8,0x30(r30)`; D3DDevice_CreateTexture 0x822DFBF4 |

### D3DVertexBuffer, 0x20 bytes (unchanged)

Allocated with size 0x20 by D3DDevice_CreateVertexBuffer 0x822DAB28 (`li r3,0x20`).

| field | offset | evidence |
| --- | --- | --- |
| resource | 0x00 | as above |
| Format (one GPUVERTEX_FETCH_CONSTANT, 2 dwords) | 0x18 | D3DDevice_SetStreamSource 0x822E2E34 `lwz r10,0x18(r5)`, 0x822E2E3C `lwz r5,0x1C(r5)`; D3DVertexBuffer_Unlock 0x822DAC88 `lwz r11,0x18(r3)`; D3DDevice_CreateVertexBuffer 0x822DABAC `stw r9,0x18(r31)` and 0x822DABCC `stw r11,0x1C(r31)` |
| Format.dword[0] is the base address | 0x18 | SetStreamSource adds OffsetInBytes to it and folds it into the fetch constant |
| Format.dword[1] is the size | 0x1C | SetStreamSource subtracts OffsetInBytes from it; D3DVertexBuffer_Lock 0x822DBD20 masks it with 0x3FFFFFC |

### D3DIndexBuffer, 0x20 bytes (unchanged)

Allocated with size 0x20 by D3DDevice_CreateIndexBuffer 0x822DACC0 (`li r3,0x20`).

| field | offset | evidence |
| --- | --- | --- |
| resource | 0x00 | as above; Common carries the endianness bits 29..30 (0x822DACDC `slwi r10,r30,29`) and the type 2 |
| Address | 0x18 | D3DDevice_CreateIndexBuffer 0x822DAD48 `stw r11,0x18(r31)`; D3DIndexBuffer_Unlock 0x822DAD58 `lwz r4,0x18(r3)`; D3DIndexBuffer_Lock 0x822DBE50 passes `a1[6]` unmasked |
| Size | 0x1C | D3DDevice_CreateIndexBuffer 0x822DAD34 `stw r28,0x1C(r31)` (Length is the first argument, r3); D3DIndexBuffer_Lock 0x822DBE50 passes `a1[7]` (0x1C) as the range end to sub_822DB958 |

### D3DSurface, 0x30 bytes (unchanged)

Allocated with size 0x30 by D3DDevice_CreateSurface 0x822DFC28 (`li r3,0x30`).

| field | offset | evidence |
| --- | --- | --- |
| resource | 0x00 | D3DDevice_CreateSurface 0x822DF06C..0x822DF078 sets Common = 4, ReferenceCount = 1, BaseFlush = -1 |
| SurfaceInfo (parent texture when FROM_TEXTURE is set) | 0x18 | D3D::SetSurfaceHeader 0x822DF198 `stw r10,0x18(r31)`; sub_822DBFE8 (the resource-range helper) takes `a2[6]` (0x18) as the parent texture on the `(Common & 0xF) == 4` path |
| ColorOrDepthInfo (RB_COLOR_INFO or RB_DEPTH_INFO) | 0x1C | D3D::SetSurfaceHeader 0x822DF1B8 `stw r11,0x1C(r31)` for EDRAM surfaces, 0x822DF25C for the resolve path; D3DDevice_CreateSurface 0x822DFCFC |
| HiControl (RB_TILECONTROL) | 0x20 | D3D::SetSurfaceHeader 0x822DF1F8 `stw r22,0x20(r31)`; D3DDevice_CreateSurface 0x822DFD20 `lwz r11,0x20(r31)` |
| Dimensions | 0x24 | D3D::SetSurfaceHeader 0x822DF190 `stw r11,0x24(r31)` (`slwi r11,(width-1),18` and `insrwi r11,(height-1),15,14`), so width is bits 31..18 and height is bits 17..3, as the header helpers assume; D3D::SetViewport 0x822E37DC reads them back the same way |
| Format | 0x28 | D3D::SetSurfaceHeader 0x822DF160 `stw r28,0x28(r31)`; D3DDevice_CreateSurface 0x822DFC84 |
| Size | 0x2C | D3D::SetSurfaceHeader 0x822DF264 `stw r11,0x2C(r31)` (the XGSurfaceSize result) |

D3DVertexShader and D3DPixelShader are read by D3DDevice_SetVertexShader /
D3DDevice_SetPixelShader only through the resource header (Common at 0, and the shared
lock/fence path), plus the microcode header at 0x368 / 0x388 that the 21256 headers
document. SetVertexShader 0x822EB670 `addic. r11,r29,0x368` and SetPixelShader
0x822EB460 `addi r11,r29,0x28` confirm the two different code-header offsets (0x368 for
vertex shaders, 0x28 for pixel shaders), which matches the constants already in
`d3d_device.h` (`kVsHeader` and `kPsHeader`).

## 6. What this means for the ported header

`native/src/render/guest/d3d_device.h` needs the following offset changes before it can
be trusted; the sizes of the resource structs and every packet member stay as they are.

    0x2A70 m_MaxAnisotropy        no counterpart found (see section 7)
    0x2A8A m_ZFilter              0x2F92, and the byte packs min/mag z filter plus the
                                  separate-z-enable bit rather than one filter value
    0x2B08 thread_owner           0x2A88
    0x2B10 writeback_page         0x2A90
    0x2B1C fence_next             0x2A9C
    0x2B3E suspend_flags          0x2ABE (bit 0x02)
    0x2B50 primary_write_index    0x2ACC
    0x2B90 queued_chunks          no counterpart found (see section 7)
    0x2FD0 vertex_declaration     0x2ED8
    0x3000 scissor_test_enable    0x2F00
    0x3004 color_write_enable     0x2F04
    0x3014 z_enable               no counterpart found (see section 7)
    0x3018 stencil_enable         no counterpart found (see section 7)
    0x301C point_sprite_enable    no counterpart found (see section 7)
    0x3024 point_size             no counterpart found (see section 7)
    0x3028 point_size_min         no counterpart found (see section 7)
    0x302C point_size_max         no counterpart found (see section 7)
    0x3044 max_mip_level          0x2F78
    0x305E min_mip_level          0x2F5E
    0x3078 high_precision_blend   0x2FAC
    0x320C index_buffer           0x3144
    0x3210 render_targets         0x3148
    0x3220 depth_stencil          0x3158
    0x3224 stream_sources         0x315C
    0x3268 stream_stride_dwords   0x31A0
    0x3278 textures               0x31B0
    0x32E0 viewport               0x3218
    0x32FC scissor_rect           0x3234
    0x330C pixel_shader           0x3244
    0x3310 vertex_shader          0x3248
    0x3BF4 suspend_lock           0x3B30
    0x5614 swap_writeback_index   0x554C
    0x5618 sizeof(D3DDevice)      0x5F00 (allocation size; last known field at 0x5E80)

Everything else in that header (m_Pending through m_PointPacket, all packet members, all
D3DConstants members, all five resource structs) holds as written.

## 7. Undetermined fields and what would settle them

1. m_MaxAnisotropy (CC2 0x2A70). Not found. D3DDevice_SetTexture in this build clamps
   only the fetch constant's MinMipLevel and MaxMipLevel; it never clamps AnisoFilter, so
   the clamp has moved into SetSamplerState (an unnamed function). Settling it needs the
   SetSamplerState_Default or SetSamplerState_MaxAnisotropy handler to be identified,
   either by matching its call site in the unnamed `SetSamplerState` dispatcher or by
   diffing the state block capture against a device built by Direct3D_CreateDevice.

2. queued_chunks (CC2 0x2B90). Not found. This build tracks outstanding indirect buffers
   in a CRingAllocList at 0x3500 guarded by the spin lock at 0x2B08, so CC2's single
   counter may not exist at all. Settling it needs a runtime trace of
   D3D::CDevice::QueueIndirectBuffers (0x822D9178) and
   D3D::CDevice::AddSegmentCallToCommandBuffer (0x822D7E00) over one frame, watching for a
   dword that rises and falls with the outstanding count.

3. z_enable, stencil_enable, point_sprite_enable, point_size, point_size_min,
   point_size_max (CC2 0x3014 to 0x302C). Not found. The GetRenderState-style getters in
   this build decode the GPU packet shadows instead of keeping dedicated bytes: for
   example sub_822E0FE8 0x822E0FE8 reads m_ControlPacket.DepthControl at 0x2934 and
   extracts bit 29, and sub_822E0978 0x822E0978 reads 0x2EFC and shifts out bit 31.
   Settling it needs the unnamed GetRenderState handlers for D3DRS_ZENABLE and friends to
   be identified (they are reachable through m_GetRenderStateCall at 0x224, so a runtime
   dump of that table after Direct3D_CreateDevice maps every render state to its getter).

4. The exact CC2 0x2B3E analogue for suspend_flags is confirmed at 0x2ABE, but the same
   byte also carries other device flags (0x822FE0DC sets bit 4 at 0x2ABE, 0x822DCBxx and
   SetPending_Shaders use bits 0x40, 0x80), so treat it as a flag byte, not a
   suspend-only field.

5. D3DResource::Identifier (0x10). Inferred from position only. Settling it needs the
   D3DResource_SetIdentifier / D3DResource_GetIdentifier pair to be identified, for example
   from the game code that tags resources before a PIX capture.

6. The interior of the private region between m_PointPacket (ends 0x2A6C) and
   thread_owner (0x2A88) was not enumerated. Something lives at 0x2A80 and 0x2A8C
   (sub_822FE040 0x822FE064, sub_822FE040 0x822FE088, sub_82306BF8 0x82306C1C), and the
   bytes at 0x2ABC and 0x2ABD are device flag bytes used by nearly every API. A full map
   would come from decompiling `sub_822FE040`, which is the device constructor called from
   Direct3D_CreateDevice 0x822DCE60.
