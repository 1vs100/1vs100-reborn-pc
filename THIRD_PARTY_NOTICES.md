# Third-party notices

This repository's own code is under the MIT License (see `LICENSE`). The parts below
keep their original licenses.

## UWP first-run onboarding (MIT)

`native/src/uwp_onboarding/` is adapted from sdl-uwp-onboarding by tuxuser
(UWP-only build): browser-upload / USB-picker file acquisition with a hash
manifest. Full license: `native/src/uwp_onboarding/LICENSE.upstream`.
Adaptations for this repository: four-field manifest (`<sha256> <size>
<kind> <path>`), user-folder store root, embeddable gate that returns to the
caller instead of running its own main loop.

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

## Not included, used at build or run time

- **ReXGlue SDK** (the static recompiler and runtime): obtained separately under its own license.

## Game content

No game code, data, audio, video or Avatar content is included. 1 vs 100 and its
content belong to their owners. You need your own copy.
