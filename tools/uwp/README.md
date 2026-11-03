# UWP build and first-run onboarding

The Store/Xbox build cannot ship game files, so the `.appx` carries no
content. On first run (and any later run with files missing or changed)
the app shows a setup screen before the game starts:

- Scan the QR code with a phone (or open the shown `http://` URL in a
  browser on the same network) and upload the files, or
- On the device: **Pick a folder** / **Pick files** (on Xbox the pickers
  need a USB drive connected).

Every file is checked against `package/manifest.txt` (SHA-256 + size) and
only verified files reach
`LocalState/content/` and `LocalState/avatar-content/`. Later starts skip
the UI when everything already verifies.

## Manifest

`package/manifest.txt` is the single file list for both this onboarding
and the desktop launcher's Files tab. Lines are
`<sha256-hex> <size> <kind> <relative/path>`, paths relative to the
user-data root (`content/…`, `avatar-content/…`).

Regenerate it from reference files:

```
mkdir -p /tmp/uwp-store
ln -s <your content dir> /tmp/uwp-store/content
ln -s <your avatar-content dir> /tmp/uwp-store/avatar-content
python3 tools/uwp/make_manifest.py --store /tmp/uwp-store \
    --kinds tools/uwp/manifest-kinds.txt --out package/manifest.txt
```

`tools/uwp/manifest-kinds.txt` curates kinds (`game`, `launch`, `show`,
`episode`, `avatar`) and file names; the script fills in hashes and sizes.
Commit the resulting `package/manifest.txt` — it is embedded in the UWP
build and shipped in the desktop release archives for the launcher.

## Code layout

`native/src/uwp_onboarding/` is adapted from sdl-uwp-onboarding (MIT, see
`THIRD_PARTY_NOTICES.md`): manifest parsing, HTTP upload server, USB/file
pickers, progress UI. It compiles into `onevs100.exe` for the
`WindowsStore` toolchain only and runs from `Onevs100App::OnConfigurePaths`
via `uwp_onboarding::EnsureGameFiles()`, before the game creates its
window. UWP-only path rules live next to the generic ones:
`content/` and avatar content resolve under the user folder
(`native/src/content_install.cpp`, `native/src/avatar_native/native_provider.cpp`).

## Packaging

`tools/package_uwp.sh <build_dir> <out_dir>` builds the signed `.appx`
from the CI `uwp` leg output plus `tools/uwp/AppxManifest.xml` (needs
`removableStorage` and `broadFileSystemAccess` for the pickers). The
package intentionally contains no game files.
