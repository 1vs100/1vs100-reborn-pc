# CrashCourse2Recomp adoption plan

Source: https://github.com/panedivitasei/CrashCourse2Recomp (commit 154bb9d), ReXGlue SDK 0.10.0 title, Windows only.

License: the repository has no license file, but its author permitted copying its code into this project. Credit CC2 in THIRD_PARTY_NOTICES.md for copied files. Its Xenia-derived files are BSD, plume is MIT, XenosRecomp is MIT.

## P0: XMA decoder never wakes when the guest frees output space

CC2 `src/audio/xma_kick.cpp`: the guest frees output-ring space with plain memory writes. The SDK decoder worker only wakes on a context kick and otherwise blocks in `rex::thread::Wait(work_event_)` with no timeout. The title then waits until an unrelated sound kicks the decoder. CC2 kicks it every 1 ms from a title thread.

This matches the intermittent silent music on Xbox: the music context decodes briefly, then gets no more work; whether another sound kicks the decoder in time depends on timing.

Steps:
1. SDK `src/audio/xma_decoder.cpp` `WorkerThreadMain`: wait with a 1 ms timeout instead of forever. One line, no title thread.
2. Console: 10 automated runs to the lobby. Pass: music in 10 of 10 (baseline: about 6 of 19).
3. Linux: confirm no regression, check CPU use of the worker.
4. Commit in rexglue-sdk `fixes/various_1vs100`. Drop the diagnostics, the UWP test-pad hook and `native/src/xact_probe.cpp`.

## P1: setjmp/longjmp through the manifest instead of `jump_hooks.cpp`

`native/src/jump_hooks.cpp` (54 KB, 24 hand-copied functions from `create_jump_hooks.py`, which is not in the repo) injects `ppc_setjmp`/`ppc_longjmp` around calls to 0x824B2FA0 and 0x824B3610. The SDK codegen supports `setjmp_address` and `longjmp_address` in the manifest (CC2 uses them).

Steps:
1. Confirm which of the two addresses is setjmp and which is longjmp.
2. Find why the other 18 hooks exist (only 6 contain setjmp/longjmp).
3. Add both addresses to `native/onevs100_manifest.toml`, rerun codegen, delete the hooks the codegen now covers.
4. Desktop and console smoke run through the lobby and one show.

## P1: declarative mid-function hooks instead of copied function bodies

CC2 declares 37 `[[midasm_hook]]` entries in `config/cc2_hooks.toml` (address, name, registers, `return_on_true`) and keeps the generated code untouched. We copy whole recompiled functions to change one spot (`jump_hooks.cpp`, `jpeg_probe.cpp`, parts of `native_room_hook.cpp`, `exit_probe.cpp`). Copies go stale when codegen changes.

Steps:
1. List every `REX_HOOK_RAW(sub_...)` that copies a body, with the one instruction it changes.
2. Convert each to a `midasm_hook` plus a small host function. Start with the probes.

## P1: exports overridden in the executable and ordinal lookups

CC2 `src/kernel/export_override.cpp`: on Windows, `GetProcAddressByOrdinal` searches the registry inside `rexruntime.dll`, which never sees `REX_EXPORT`s defined in the executable. Lookups by ordinal then reach the SDK's implementation, not the title's. Our log shows ordinal lookups (`XamShowPartyUI`, `XamParty*`).

Steps:
1. List our `REX_EXPORT` overrides (`guest_sockets.cpp`, `gameserver_*.cpp`, `offline_profile.cpp`).
2. Check which are also resolved by ordinal at runtime, on Windows and UWP.
3. If any: fix it in the SDK (one registry shared by runtime and executable) rather than patching the DLL's registry from the title as CC2 does.

## P2: new guest threads inherit the parent's CPU

CC2 `src/kernel/xboxkrnl/thread_affinity.cpp`: a thread created without a processor mask runs on the parent's CPU on hardware. The SDK round-robins it across six fake cores. CC2 sets the mask in an `ExCreateThread` hook.

Steps: measure on the console only after P0. Prefer an SDK change over a title hook.

## P2: compile-time check of the SDK surface

CC2 `src/cc2/sdk_compat_check.cpp` holds `static_assert`s for every SDK API it uses. We build against a moving SDK branch. A small equivalent for the APIs we use (`downmix.h`, content manager, hook macros, cvars) fails the build when the SDK moves them.

## P3: native D3D12 renderer with precompiled shaders

CC2 replaces the SDK GPU path: an "edge" layer stands in for the XDK D3D runtime, records draws, and renders them with plume (D3D12/Vulkan) using shaders recompiled ahead of time by XenosRecomp into a shader pack. This is the UnleashedRecomp approach.

Our earlier in-process renderer was stopped (no gain over the SDK backend, see memory). CC2 is a complete working reference with a different design (no translation at run time). It is the largest possible gain for Series S frame rate and also the largest effort. Decide after P0 to P2, based on a frame-time profile on the console.

## P3: in-game settings through injected Lua menus

CC2 injects Lua menu definitions (`display_menu_lua.h`, `lobby_menu_lua.h`) for display and lobby settings. We already register native Lua globals (`native_room_hook.cpp`). Useful for UWP, where no config file is editable by the player.

## Not applicable

- Online through jmlive / Game Jolt, Sentient, famestar, lobby relay: we have our own gameserver.
- Hong Kong DLC `NtOpenFile` filter, content header synthesis: CC2-specific; our content registers through the SDK.
- XAudio2 backend: only if the SDL backend shows a Windows problem after P0.
- Avatars: CC2 uses the Avatar Editor Recomp code (about 7.8 k lines); ours is Nexia-based (about 10.5 k lines) and works. No change.
- Mouse UI, freecam, keyboard bindings.
