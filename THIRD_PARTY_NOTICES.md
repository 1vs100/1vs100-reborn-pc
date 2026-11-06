# Third-party notices

This repository's own code is under the MIT License (see `LICENSE`). The parts below
keep their original licenses.

## UWP first-run onboarding (MIT)

The first-run file setup view lives in its own repository,
[1vs100/deployment-onboarding-ui](https://github.com/1vs100/deployment-onboarding-ui),
and is checked out here at `native/third_party/deployment-onboarding-ui`. It is
adapted from sdl-uwp-onboarding by tuxuser: browser-upload and USB-picker file
acquisition with a hash manifest. Its own notices, including the upstream MIT
license, are in that repository.

## Nexia / Xenia Avatar format code (BSD 3-Clause)

`native/src/avatar_native/` (`xna_avatar_format.*`, `native_assets.cc` and related
files) is adapted from the Nexia/Xenia project's Avatar code.
Copyright (c) 2015, Ben Vanik. All rights reserved.
Full license: `native/src/avatar_native/NEXIA_LICENSE.txt`.
Changes in this repository (installed Avatar items, lazy loading, party Avatars,
similar-item substitution) are under the same terms.

## libmspack LZX decompressor (LGPL 2.1)

`native/src/avatar_native/third_party/mspack/` is libmspack by Stuart Caie, used
unmodified apart from build glue (`logging.cc`, `config.h`).
License: `native/src/avatar_native/third_party/mspack/COPYING.LIB`.

## CrashCourse2Recomp XAudio2 backend

`native/src/audio_xaudio2.{h,cpp}` is copied from [CrashCourse2Recomp](https://github.com/panedivitasei/CrashCourse2Recomp) with its author's permission. It was ported there from masterspike52/reDAHM and is based on Xenia's XAudio2 driver (BSD 3-Clause).

## plume (RHI)

`native/third_party/plume` is [plume](https://github.com/renderbag/plume) by renderbag and contributors, MIT. It is vendored with the D3D12 backend only: `plume_vulkan.cpp` is excluded from Windows builds, and the swap chain takes the CoreWindow path under `PLUME_UWP` (CreateSwapChainForCoreWindow, no window association). The UWP approach follows [plume-uwp](https://github.com/worleydl/plume-uwp) commit d40b98a.

## XenosRecomp (MIT)

`native/third_party/xenosrecomp` is [XenosRecomp](https://github.com/hedge-dev/XenosRecomp) by
hedge-dev and contributors, MIT, as vendored by CrashCourse2Recomp. It translates Xenos shader
microcode to DXIL at run time and reads the X4 shader pack. Its own third-party code keeps its
licenses: fmt, xxHash, zstd, smol-v (see `native/third_party/xenosrecomp/thirdparty`), and the
Microsoft DirectXShaderCompiler binaries (`thirdparty/dxc-bin`, LLVM/MIT).

## GLM

`native/third_party/glm` is [GLM](https://github.com/g-truc/glm), MIT.

## Not included, used at build or run time

- **ReXGlue SDK** (the static recompiler and runtime): obtained separately under its own license.

## Game content

No game code, data, audio, video or Avatar content is included. 1 vs 100 and its
content belong to their owners. You need your own copy.
