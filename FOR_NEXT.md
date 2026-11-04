# FOR_NEXT.md

Renderer state and the remaining port steps are in `docs/d3d-renderer-port.md`, and the
verified guest device layout is in `docs/d3d-device-layout.md`. The sections below record the
audio and guest GPU work that preceded them.

## State after the native renderer work (2026-11-03, ~22:45 UTC)

No active subagents. Repo pins:

- 1vs100-PC main = `017e966` (`41451d2` XAudio2, `7cd3f6f` XMA kicker, `87c9007` native
  renderer on the console, `017e966` test pad from LocalState). Nothing pushed yet.
- rexglue-sdk fixes/various_1vs100 = b81145d plus uncommitted diagnostics, including a
  `CreateComputePipelineState` HRESULT log in `src/ui/d3d12/d3d12_util.cpp`.
- UWP manifest in the tree is 0.1.149.0 (test version, uncommitted).
- Console has OneVs100.Native 0.1.149.0 installed, config `native_renderer = false`.

## Fixed and committed

- XAudio2 output backend on Windows and UWP (`native/src/audio_xaudio2.{h,cpp}`), selected in
  `OnPreSetup`. Verified on the console: device and mastering voice are created, output level is
  non-zero, UI sounds and ads are audible. Imports XAudio2_9.dll.
- XMA decoder kicker (`native/src/xma_kick.cpp`, `precise_sleep.h`): 1 ms sweep that writes the
  context-kick register with an empty mask, so the runtime's decoder worker, which otherwise has no
  re-poll timeout, re-checks every context. `SleepPrecise` uses a high-resolution waitable timer;
  plain Sleep rounds to the 15.6 ms tick.

Console measurements, 4-5 runs per variant, "stream" = the game read 64 KB music.xwb chunks:

| variant | music stream | crowd stream |
|---|---|---|
| no kicker (0.1.115/116.0) | 0/5 | 0/4 |
| coarse 1 ms kick (0.1.117.0) | 2/4 | 3/4 |
| precise kick + parent-CPU affinity (0.1.118.0) | 1/3 | 0/3 |
| precise kick, no affinity (0.1.119.0) | 4/5 | 2/5 |
| precise kick + review fixes (0.1.120.0) | 1/6 | 2/6 |
| Linux build, SDL backend (same code) | 4/4 | 3/4 |

The 0.1.119.0 and 0.1.120.0 samples disagree far more than the change between them can
explain (the only difference is the review fixes in the XAudio2 driver and the kicker
start/stop locking). Treat the console rate as noisy and unmeasured, not as a regression.
On Linux the same code has streamed music in every run of the last two batches.

## Rejected

- Guest thread CPU affinity inheritance (CC2 `kernel/xboxkrnl/thread_affinity.cpp`, ported into
  `ExCreateThread_entry`): a thread created without a processor mask takes the parent's CPU instead
  of the runtime's round-robin. It made both streams worse on this title (table above), so it was
  reverted and is not committed. Do not retry without a reason.

## Open: commentary voice drops

Symptom: pressing A to join plays no host voice. Measured, not guessed:

- Every run issues the same single `vo.xwb` read (131072 bytes at offset 10190848) and it succeeds.
- The XACT cue `EP_02_JIP_01_Welc` lasts 0.16 s in failing runs, 2.0 s and 7.5 s in others.
- In the failing runs the mono 48 kHz XMA context for that cue (36 input packets, 16 output blocks)
  never appears in the `DIAGCTX` dump. When the voice plays, it appears once per 500 ms.
- A stereo context (32 packets, 25 output blocks) keeps decoding in every run with its output ring
  full (`out=1`, `wr`/`rdo` one apart), so the guest is not draining that one.

So the commentary stream is dropped at the engine's stream start, the same place the music was
dropped before the kicker, not in the output device.

A second, coarser failure mode: in 3 of the 6 console runs of 0.1.120.0 no XMA context was allocated
at all for the whole session (`DIAGCTX` produced 9-11 lines instead of 60-280), so no streamed wave
of any kind ran while UI sounds and ads still played. That points at a one-time init race in the
title's streaming subsystem, not at a per-cue decision. The music now survives 4 of 5 runs; the voice
does not. Next step is guest-side visibility of the stream start: the read helper is
`sub_8232DD68` and its return address appears as `lr=8232DDF0` in the `DIAG read` log; callers of
interest are in `native/generated/default-v5`. `native/src/io_probe.cpp` hooks the wrong wrapper
(`sub_8250B918`); the audio reads do not go through it.

## Native renderer (UWP first)

Committed in `f20eb13` (amended): the title's own D3D12 renderer in `native/src/native_gpu/`,
selected with `native_renderer = true` (default off). Uncommitted on top: `guest_gpu.{h,cpp}`,
the ring walker, the plume UWP fixes and the diagnostics described below.

State, all measured on the console:

- Presentation path compiles and runs from the UI thread; the CoreWindow swap chain matches
  worleydl/plume-uwp d40b98a (credited in THIRD_PARTY_NOTICES.md).
- **A package that statically imports `d3d12.dll` cannot be activated on the console**: the app
  bounces back to Dev Home with no log. Fixed with `LINKER:/DELAYLOAD:d3d12.dll` + `delayimp`
  for WindowsStore. This was the cause of the "launch 400 / bounces back" episode, not the code.
  `dxgi.dll` is imported statically by the SDK runtime too and is fine.
- Guest GPU shim (`GuestGpu`): maps `0x7FC80000` MMIO, answers register reads, walks the primary
  ring (type 0/1/2/3, EVENT_WRITE_SHD `0x58`, WAIT_REG_MEM `0x3C`, dword indices, wrap) and
  publishes the read pointer via `TranslatePhysical` + byte swap; vblank is an `XHostThread`
  dispatching the graphics interrupt (source 0, cpu 2) with an incrementing counter.
  With `native_renderer_present = false` the guest **boots and runs** against it:
  `read == write` (ring drained), no stalled waits, audio alive.
- Presentation on (`native_renderer_present = true`) now comes up: device, 1920x1080 swap
  chain and a cleared frame reach the display (dark blue 0.05/0.07/0.15). Three plume fixes
  were needed, all committed: `/DELAYLOAD:d3d12.dll`, base `ID3D12Device` + QueryInterface to
  `ID3D12Device8`, and no `CreatePipelineLibrary` on UWP (that call removes the device, so
  every later PSO creation and even `CreateCommandQueue` failed with 0x887A0005).
- Only a clear is presented; guest draws are not executed yet, so the screen stays flat.
- `native_renderer = false` now really selects the plugin: the setting's value is read, not
  merely its presence.

### The xenos plugin renders the title

With `native_renderer = false` the SDK D3D12 plugin runs on the console at the 60 Hz cap:
`draws=960 shaders=1320 textures=1380 resolves=240 clears=240` per second, i.e. about
16 draws, 22 shader binds, 23 texture binds and 4 resolves/clears per frame. The intro and
the legal notice render correctly. The headless "Failed to create Resolve Copy Fast 32bpp
1x/2xMSAA resolve copy pipe" failure no longer occurs, and the guest GPU shim is not
instantiated on that path.

Next step: log `GetDeviceRemovedReason()` and the adapter identity (the edit for this failed to
apply last time), then compare against the SDK path step by step. If the console refuses
`ID3D12Device8`, plume needs a lower device interface (Device5+ APIs are used for
CreatePipelineLibrary, descriptor heaps, `CreateCommittedResource`).

Note: repeated launch crashes leave the package unlaunchable until the console is rebooted and
the account signed in again; a portal reboot is `POST /api/control/restart` with
`Content-Length: 0`.

## Diagnostics in the working tree, not committed

- `rexglue-sdk` `xboxkrnl_io.cpp`: `DIAG open` for `.xwb` opens and `DIAG read` with caller LR for
  `.xwb` reads, plus `DIAG read dropped` for reads whose file or event handle lookup fails.
- `rexglue-sdk` `xma_decoder.cpp`: `DIAGCTX` 500 ms dump of every allocated context, now including
  the decoded output peak.
- `rexglue-sdk` `sdl_audio_driver.cpp`: `DIAG out` peak log (SDL backend only).
- Game: `native/src/io_probe.cpp` (wrong wrapper, see above), `diag_skip_ui_cues` in
  `dialogue_hook.cpp`, `test-pad.txt` support in `native_controls_posix.cpp`, `DIAG out xaudio2`
  peak log in `audio_xaudio2.cpp`.

The UI-cue-skip experiment is finished and negative: with UI cues skipped the console still lost
music (2 of 3 runs played). Remove the cvar and the hook when done.

## Testing on the console

Everything used before is still in the scratch dir of the previous session
(`/tmp/claude-1000/-home-e99-Development-personal-x360-1vs100-public-1vs100-PC/efd70973-.../scratchpad`):
`xbox.sh` (Device Portal helpers `xb_auth`, `xb_launch`, `xb_press`, `xb_log`) and `xa_runs.sh`
(`xa_runs.sh <count>`, edit `XV` to the installed version). Build with `cmake --build build/uwp`,
package with `MAKEMSIX=~/.local/share/msix-packaging-build/bin/makemsix tools/package_uwp.sh
build/uwp build/uwp-package`, bump the manifest version above the installed one, upload with
`xb_auth` + `POST /api/app/packagemanager/package`, then upload `onevs100.toml` to LocalState.

Each run takes about 2 minutes: launch, 30 s, four A presses 5 s apart, 35 s, one more press, 25 s,
then fetch the newest log from LocalState\logs. Music presence is
`grep -c 'DIAG read music'` on that log; the "MUSIC : Calling" line only says the Lua music
sequence started.
