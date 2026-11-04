# Gameserver Manifest-Only Restructure Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Keep gameserver play plus AvatarMetadata manifest avatars; drop avatar editor, Steamworks, offline playback.

**Architecture:** Delete room/Steam/editor-only sources; shrink shared files to gameserver plus manifest paths; leave render catalog intact.

**Tech Stack:** CMake C++23 ReXGlue, Win32 launcher, PowerShell tools, Lua staging.

**Spec:** User requests in thread 2026-11-02: drop avatar-editor, Steamworks API, offline-playback; keep gameserver functionality and avatar reading from AvatarMetadata manifest.

## Global Constraints

- ASCII only in code, comments, UI strings, commit messages.
- No separator or banner comments; terse WHY comments only.
- Short focused commits, no AI attribution.
- Adversarial review with fresh subagent before commit.

## Review Focus

- Stock retail Lua under gameserver never calls NativeRoomTransport; removed thunk must not break startup.
- Guest socket and matchmaking paths still resolve gameserver host after online_service simplification.
- Manifest relay still round-trips when local avatar.bin is absent; Default seeded avatar covers that.
- Deleted launcher modes leave no dead PowerShell ValidateSet entries.
- No remaining reference to deleted symbols or env vars in CMake, C++, Lua, ps1.

---

### Task 1: Strip Steam room transport and room Lua/tools

**Files:**
- Modify: `native/CMakeLists.txt`
- Modify: `native/src/native_room_hook.cpp`
- Delete: `native/src/native_room_steam.cpp`, `native/src/native_room.h`, `native/src/room_clock.cpp`, `native/src/room_clock.h`, `native/src/native_room_unavailable.cpp`
- Delete: `lua/NativeRoom.lua`, `tools/room/native-room-service.lua`, `tools/room/steam_transport_test.cpp`, `tools/room/build.cmd`, `tools/room/native_room_test.cpp`, `tools/room/room_authority.h`, `tools/room/room_authority_test.cpp`, `tools/test/native-room-test.lua`, `tools/test/native-liveroom-test.lua`, `tools/test/native-party-test.lua`, `tools/test/native-party-avatar-test.lua`, `tools/test/native-clock-test.lua`, `tools/Launch-NativeRoomTest.ps1` room modes
- Modify: `launcher/launcher.cpp`

**Interfaces:**
- Consumes: none.
- Produces: `native_room_call` Lua op keeps `avatar/avatarof/storage/debug` only; no `NativeRoomTransport`, no `ONEVS100_ROOM_MODE`.

- [ ] **Step 1: Delete room/Steam sources listed above**
- [ ] **Step 2: Update CMakeLists to drop Steam SDK block and room sources**
- [ ] **Step 3: Shrink native_room_hook to avatar plus storage and debug only**
- [ ] **Step 4: Strip launcher Steam UI and room launch modes**
- [ ] **Step 5: Configure cmake to verify no missing references**

### Task 2: Strip offline playback, keep gameserver branches

**Files:**
- Modify: `native/src/online_service.cpp`, `native/src/online_service.h`
- Modify: `native/src/offline_network.cpp`, `native/src/offline_profile.cpp`
- Modify: `native/src/local_profile.cpp`, `native/src/local_profile.h`, `native/src/local_profile_internal.h`, `native/src/local_profile_dialog_posix.cpp`, `native/src/local_profile_dialog_win32.cpp`
- Delete: `lua/NativeLiveBots.lua`, `tools/room/offline-session-lifecycle.lua`, `tools/test/native-livebots-test.lua`

**Interfaces:**
- Consumes: Task 1 launcher changes.
- Produces: `LoadOnlineService` gameserver-only; profile keeps `LocalXuid/LocalGamertag` without Steam or editor stamp.

- [ ] **Step 1: Force online_service to gameserver; drop Offline enum value**
- [ ] **Step 2: Simplify offline_network/profile to gameserver behavior**
- [ ] **Step 3: Remove Steam name and editor stamp from local profile**
- [ ] **Step 4: Update callers and verify grep finds no Offline/Steam refs**

### Task 3: Strip avatar editor, keep manifest reading

**Files:**
- Delete: `avatar-editor/` (build plus Import scripts plus README)
- Modify: `native/src/avatar_native/local_avatar.cpp`, `native/src/avatar_native/local_avatar.h`
- Modify: `native/src/avatar_probe.cpp`, `native/src/exit_probe.cpp`, `native/src/native_room_hook.cpp`
- Delete: `native/src/avatar_native/catalog_test.cpp`, `native/src/avatar_texture_upload.cpp`, `native/src/avatar_model_probe.cpp`, `native/src/avatar_joint_derive.cpp`, `native/src/avatar_joint_probe.cpp`, `native/src/avatar_texture_upload.h`, `native/src/native_gpu/draw_capture.cpp`, `native/src/native_gpu/frame_dump.cpp`
- Modify: `native/CMakeLists.txt`, `launcher/launcher.cpp`, `tools/test/native-storage-test.lua` keep

**Interfaces:**
- Consumes: Tasks 1-2.
- Produces: `local_avatar::Load/Save/Default` via `avatar.bin` only; `NativeAvatarManifestHex/SetRemote/RemoteManifest` intact.

- [ ] **Step 1: Delete editor dir and probe-only avatar sources**
- [ ] **Step 2: Shrink local_avatar to avatar.bin plus Default**
- [ ] **Step 3: Update CMake and launcher to drop editor targets and buttons**
- [ ] **Step 4: Build avatar-related targets to verify manifest path intact**

### Task 4: Verify and commit

**Files:**
- Modify: `README.md`, `docs/gameserver-mode.md` as needed for accuracy

- [ ] **Step 1: Run cmake configure plus target build**
- [ ] **Step 2: Run adversarial review with fresh subagent**
- [ ] **Step 3: Commit focused change with short message**
