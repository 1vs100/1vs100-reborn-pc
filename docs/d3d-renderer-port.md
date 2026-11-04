# Native renderer port: state and remaining work

The renderer is ported from CrashCourse2Recomp (CC2) rather than rewritten, so its
behaviour and its module split match a working title. This replaces the earlier attempt at
an in-process renderer of our own design.

## What CC2's renderer is

`config.graphics = nullptr` and no GPU plugin: the title's XDK D3D runtime runs recompiled
with no GPU behind it. It never reads GPU registers, so no MMIO emulation is needed. The
ring is written but nothing executes it. The runtime's own waits are satisfied in the edge
layer:

- After the fence counter store in `D3D::CDevice::SetFence`, the edge writes the fence
  value and its ring position into `device->writeback_page`, which is what the GPU would
  have written. Fence waits therefore complete immediately.
- After the store of the primary write index in `CDevice::AddCallsToPrimaryBuffer`, the
  edge writes that index into the read-pointer slot at `writeback_page + 0x3C`, so
  `BlockOnPrimaryRange` never waits.
- After each increment of the outstanding chunk count in `CDevice::QueueCommands` and
  `QueueIndirectBuffer`, the edge resets it to zero, so `BlockUntilIdle` returns.
- `D3DDevice_InsertCallback` and `KeInsertQueueDpc` run their routine inline, since the GPU
  would have run them.
- `VdSetGraphicsInterruptCallback`, `VdInitializeRingBuffer` and
  `VdEnableRingBufferRPtrWriteBack` are owned by the edge and are no-ops.

The instruction-level parts use the SDK's `[[midasm_hook]]` entries: address, host function
name and the registers to pass. CC2 declares them in `config/cc2_hooks.toml` and leaves the
generated code untouched.

Draws, clears, resolves and swaps are captured by function hooks on the runtime's entry
points, which build records. A render thread executes the records with plume and presents
at swap.

## The renderer draws the title

The guest runs with no graphics system, the edge hooks satisfy its waits, the draw
and resource entry points are captured, and each shader is translated on first use
with XenosRecomp (the compiler ships in the package and the cache lives in the user
folder). The console shows the lobby screen, with text, avatars and the show sign
drawn correctly.

Open item: the title renders 880 of the front buffer's 1280 columns and leaves the
video scale to the XDK scaler, which never runs without a GPU. The capture now tracks
the region its resolves cover and the swap record carries it, but the present still
places that region as if it were the whole front buffer, so the frame shows black
bars instead of filling the screen. Next step is the swap blit itself: in
`Backend::Execute(const SwapRecord&)` the source rect branch must reach `Blit` with
the full back buffer as the destination, and the result compared against the
`record swap ... scaler=0,0,880,720` log line.

## Measured state at the port

- The ported renderer starts: device and swap chain come up on `SraKmd_arden`.
  `native/src/render/`, commit `c4e4f49`.
- Draws are dropped until a shader pack exists (`[gpu] no usable shader pack`).
- The interim guest GPU (`native/src/native_gpu/guest_gpu.cpp`, MMIO plus a ring walker)
  keeps the guest alive but the title never renders through it: the first submission is
  `ME_INIT` followed by three `PM4_INDIRECT_BUFFER` packets, and the runtime then waits on
  work that lives inside those indirect buffers. The ring is idle afterwards (ring
  telemetry: `read == write`, no draws, no shader loads) and the title's D3D entry points
  are never called for drawing.
- This is the reason the ring-walker design is dropped: CC2's model needs no ring at all.

## Device layout

`docs/d3d-device-layout.md` holds the verified offsets and the address map from CC2's guest
addresses to this title's, produced from the retail disassembly. The device header through
`m_PointPacket` is identical to CC2's model; the tail shifts (`-0xC8` for the resource and
state block, `sizeof(D3DDevice) == 0x5F00`).

## Remaining work, in order

1. Remove the interim guest GPU. Set `config.graphics = nullptr` and clear the plugin in
   `Onevs100App::OnPreSetup`, delete `native/src/native_gpu/`, and start the renderer from
   the app shell with the window handle, as CC2 does.
2. Add the fence, primary-kick, chunk-count, `InsertCallback` and `KeInsertQueueDpc` hooks,
   plus the three `Vd*` no-ops. The instruction addresses have to be recovered for this
   build; `D3D::CDevice::SetFence` is at 0x822D8FC0 and `CDevice::AddCallsToPrimaryBuffer`
   is near it (see `docs/d3d-device-layout.md`, `writeback_page`).
3. Write the edge capture for this title: draws (`0x822F3440`, `0x822F3830`, Begin/End at
   `0x822F29A8`/`0x822F2988`, immediate variants `0x822F2E40`/`0x822F33A0`), clear
   (`0x822E5C58`), resolve (`0x822E74D0`), swap (`0x822F50C0`), the stream, texture and
   shader setters, and the resource creation and lock entry points from the address map.
4. Build a shader pack. Harvest each bound shader's microcode (vertex shaders as patched for
   their vertex declaration, via `D3D::PatchVertexShaderToMatchVertexDeclaration` at
   0x823038D8), translate it with the vendored XenosRecomp, and emit a directory pack
   (`manifest.csv` plus `<hash>.<vs|ps|vst>.dxil`). The renderer already looks for
   `shaderpack/onevs100.pack` next to the executable and falls back to the source tree.
5. Verify on the console in this order: clear and swap, then draws, then textures, then the
   overlay. Remove `native/src/render/edge_watch.cpp` once the capture covers the same entry
   points.

## Notes

- `dxcompiler.dll` and `dxil.dll` ship beside the executable and are delay-loaded; the
  package includes both.
- The plume UWP specifics (CoreWindow swap chain, base device plus QueryInterface to
  `ID3D12Device8`, no `CreatePipelineLibrary`) are in `87c9007` and still apply.
