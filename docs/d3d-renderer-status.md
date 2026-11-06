# 1vs100-PC native D3D12 renderer - status, issues, testing

Single reference for the native renderer port (CrashCourse2Recomp lineage).
Supersedes the scattered notes in `docs/d3d-renderer-port.md`
and session scratchpads; those remain as history.

## 1. Goal

Ship a native D3D12 renderer for 1vs100-PC on Xbox consoles that renders
the game correctly: boot splash, menus, lobby 3D, and live gameplay at
full frame, with no guest crashes and no hangs. The renderer replays the
title's D3D9 API stream on the host GPU (plume/D3D12 backend) instead of
emulating the GPU (the `xenos` plugin path, `native_renderer=false`).

Current state (build 0.1.270):

- Boots reliably; no guest access violations.
- 3D fills the frame; menus and press-start show the blurred 3D scene.
- The boot loading screen renders.
- Live gameplay is unverified (I3).

## 2. Open issues

### I1. Left strip in 3D scenes - FIXED (0.1.248)

Cause: `tracker::Surface` cached host descs by `SurfaceKey` without the
host extent. A band surface looked up before `BeginTiling` registered it
kept its band-sized (880/440) desc forever, so draws and per-tile
resolves were clipped to the band. The key now carries width/height.
Console-verified: draw targets and resolves are 1280x720, lobby 3D
fills the screen.

### I4. 3D background black behind UI overlays - FIXED (0.1.260)

Cause: the FXL post passes (DownSample 0x821E6678, GaussBlur5x5
0x821E6920) bind their source with `SetTexture(0, src)`, then
`FXLEffect_Commit` calls `SetTexture(0, NULL)`, which clears only the
fetch constant's type bits (type 2 -> 0) and keeps the address. The
hardware samples such a constant anyway; the draw path treated type 0 as
unbound, so the blur chain read a blank and the blurred scene behind
menus came out black. `SamplesAsTexture` now accepts kInvalidTexture
(the SDK's `gpu_allow_invalid_fetch_constants` behaviour).
Console-verified: press-start and menu show the blurred 3D background.

### I2. Loading screen black - FIXED (0.1.270)

Cause: without a shader pack, runtime translation had no geometry
shader built-ins. The loading screen's UI draws use user clip planes,
which need the `clip_planes` GS, so `DrawPipeline` cached a null
pipeline and every such draw was dropped. The built-ins (`clip_planes`,
`rect_expand`, `point_expand`) now compile on first use. Console-verified:
the loading screen, the lobby big screen and the gamertag text render.

Open: the lobby big screen plays `media\ads\*.wmv` through the title's
own decoder (several files absent); check that the ads play once the
show is live.

### I3. Matchmaking never fills (peers=0) - OPEN, blocks gameplay verify

The title sits in the loading lobby (`Matchmaking: group 1, remote
roster peers=0`, frozen progress bar). Live gameplay (countdown, full
lobby) is therefore unverified on native. Unknown whether this is
environmental (no live session on the test server at 3-6am) or
renderer-induced (1fps-class pacing breaking timeouts). The plugin run
reached a live session once, which suggests environmental, but it is
not proven. Decisive test: reach any full-3D live state natively
(session, attract mode, tutorial) and check for the strip there.

### Closed issues (do not regress)

- Boot hang from unvalidated guest addresses: fixed with `ValidGuest`
  + `QueryProtect` commit check in `gpu.h` (+ `NoteBadGuest`
  rate-limited logging). Fault PCs resolved via `native-fault.log`
  against `onevs100.map`.
- Fence publication skew: publish `r8+2` to match the runtime's
  pre-advanced fence (`0x822D9084`); waits now complete in lockstep.
- Present scaling: the present maps the resolved region (resolve
  coverage union, clamped to the front) across the back buffer.
- `clear_stencil` read from the wrong register: fixed to the stack slot
  (`r1+0x5C`) per the reference port.
- Cross-scale resolve branch (1024 shadow scale): ported from CC2.
- Not needed (dropped after console test): a `BlockUntilIdle`
  replacement, a packed-colour `Clear` (0x822E5D80) hook (it calls the
  hooked ClearF, so clears were doubled) and command-buffer hooks
  (Begin/End/Run/SetPredication have no callers in this binary).

## 3. Methods of testing

All testing is live on the console unless stated. Portal:
`https://10.0.0.77:11443`.

### 3.1 Deploy + configure + launch

- Package: `MAKEMSIX=~/.local/share/msix-packaging-build/bin/makemsix tools/package_uwp.sh build/uwp build/uwp-package` after bumping
  `Version=` in `tools/uwp/AppxManifest.xml` (version must increase or
  the portal keeps the old build).
- Deploy: `POST .../api/app/packagemanager/package?package=onevs100.appx`
  with `-F file=@build/uwp-package/onevs100.appx`.
- Config: `POST .../api/filesystem/apps/file?...&path=%5CLocalState`
  with the toml file. The toml keys used: `native_renderer`,
  `log_level`, `fullscreen`, `log_flush_interval`,
  `render_edge_log`, gameserver host/online service. Wait ~60s after
  deploy (processing), verify the config read-back contains the flag
  (config uploads are wiped by upgrades).
- Launch: `POST .../api/taskmanager/app?appid=<base64>&package=<base64>`.
  The `xbox.sh` scratchpad helpers wrap all of this (`xb_launch`,
  `xb_log`, `xb_shot`, `xb_press <hexmask>` for pads; A=0x1000,
  B=0x2000, X=0x4000, Y=0x8000, Start=0x10, Back=0x20, dpad=0x1..0x8).

### 3.2 Logs

- Pull: `GET .../api/filesystem/apps/files?knownfolderid=LocalAppData&packagefullname=$PFN&path=%5CLocalState%5Clogs`
  then the newest `onevs100_*.log` (rotation: ALSO check for
  version-named logs; early-boot lines live in the oldest file).
- Frame summary (render thread, 1/swap): `frame draws=... targets=..WxH
  vp=.. sc=.. resolves=.. front=WxH(id)xH union=.. src_rect=..` - the
  primary per-frame health line.
- Throttled detail: `record swap/clear/resolve/draw` (first 24 records
  only - boot), `present scaled` (every 60th frame), resolve/draw
  detail (every 60th frame, capped). Enough to map band tiles, dests,
  and viewport/target distributions per scene.
- Edge counts: `render_edge_log=true` gives per-entrypoint per-second
  lines (`EDGE ...`, `wait BlockOnFence#..` with waited/published/
  completion). `guest blocked: N frames queued` = backpressure state.
- Crash forensics: `native-fault.log` (fault PC + image base) resolved
  against `build/uwp/onevs100.map` via offset arithmetic
  (`true = listed + 0x1000`); `death_marker` files bracket boot phases.

### 3.3 Screenshots + pixel measurement

- Portal screenshot (`xb_shot`), 1920x1080 PNG. Measure the content
  edge: brightest column gradient (sharp 1.0->0.0 drop = clip, not dark
  scene). Convert screen px to guest px via the present mapping
  (source 880 -> dest 1920 means content_x_guest = x/1920*880).
- Reference shots live in the scratchpad (`t*.png`, `ph_*.png`,
  `plugin_*.png`). Compare the SAME game state across renderers;
  cross-state comparisons (loading vs live) are invalid - the strip
  width varies by scene (boot 1280, menu 880, loading 440).

### 3.4 Console experiments run so far (decisive ones)

- Red-clear present: no red shows through => the blit covers the full
  back buffer (present path faithful).
- Present tgt=1 (draw target) directly: black => the band content does
  not reach the draw targets OR draws paint black.
- Drop resolve-sampling draws: everything goes black => composites are
  load-bearing for all visible content.
- Drop 440-wide draws: avatars vanish => the screen 3D comes through
  the 440-band path.
- Force depth ALWAYS: wider strip (658->804) but still strip => depth
  culls some content, not the root cause.
- Force texture re-upload every draw: no change => not a stale-cache
  race (or the uploader is broken in a way re-upload cannot fix).
- Plugin-vs-native at the SAME loading state: never completed (portal
  errors); the one plugin-vs-native pair compares different states and
  is therefore void.

### 3.5 IDA verification (idalib REPL)

- DB: `1vs100-retail.xex.i64` via `./ida_exec.sh`, read-only, driver
  files in the DB dir (never in `tools/ida/` - path mismatch deadlocks
  the driver). 120s timeouts; verify behaviour, never trust fuzzy
  debug-build symbol names.
- Confirmed: `BeginTiling 0x822F5CF0` (r3=device, count@0x327C,
  rects@0x3280+i*0x10, bands@0x3264..+0x3278), `EndTiling 0x822F6380`,
  packed `Clear 0x822E5D80`, all four cmdbuf entries, scaler struct
  `D3DVIDEO_SCALER_PARAMETERS` 0x1C at Swap r5 (never field-read in
  Swap; cached at device+0x3618), `Present 0x822F5734` (r3 only, no
  rects).

### 3.6 Subagent reviews

- Drift analysis vs CrashCourse2Recomp + AvatarEditorRecomp (one
  lineage): keep the port's edge/capture/tracker split; the only
  structural gap is band assembly.
- Analysis reviews caught: H3 (present mismatch) geometrically
  impossible; per-front union can only narrow; plugin comparison void
  across states; "cmdbuf has no callers" false (module wired, only the
  four producers were missing).

## 4. Architecture (30 seconds)

`edge*` (guest hooks, fence/primary/chunk/tiling/waits) -> `gpu_capture`
(Clear/Resolve/Swap/cmdbuf records, guest thread) -> `gpu_tracker`
(guest surface/texture identity, resolve cache, tiled map) ->
`gpu_renderer` (plume/D3D12 backend, render thread: draws, resolves,
presents). Present fits the source rect across the back buffer. Key
files: `native/src/render/gpu_{renderer,capture,draw,tracker,memory}.cpp`,
`edge*.cpp`, `guest/d3d_device.h`, `native/render-hooks.toml`.

## 5. Open questions / next steps

1. Lobby big screen ads (title-decoded WMV): verify during a live show.
2. Matchmaking stall (I3): the server offered a live session once
   ("scheduled game will end soon"), so it looks environmental.
3. Review leftovers (latent, not observed):
   - BeginTiling's own clear covers only the largest band rect; the rest
     of the full-size target relies on EndTiling's per-band
     resolve-clears from the previous frame.
   - The tiled map lives until Swap, so an untiled use of a band surface
     between EndTiling and Swap gets the full-size host target.
   - `GpuBeginShaderConstantF4` writes constants into the ring only
     (Lua binding 0x821DE3D8, FXL ApplyShadowPass 0x821E75E8,
     0x825B0430, 0x825B1730); those draws read stale constants.
   - `BlockUntilIdle` spins on the chunk counter at device+0x2B04
     (written at 0x822D9120/0x822D915C); CC2 zeroes it at the same kind
     of site.
   - Present stretches the front to the back buffer; CC2 fits it to the
     display aspect.
