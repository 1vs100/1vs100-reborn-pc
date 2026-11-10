# Building releases

- One command builds a target the way CI does, and a second one packs it.
- The Linux x64, Windows and Xbox/UWP builds run in a container, so any Linux host with [Podman](https://podman.io) or Docker gets the same toolchain as CI.
- The macOS and Linux ARM64 builds run on the host.

## Usage

Clone with submodules and put your `default.xex` at `native/assets/default.xex`:

```
git submodule update --init --recursive
tools/ci/container.sh tools/ci/codegen.sh
tools/ci/container.sh tools/ci/build.sh uwp
tools/ci/container.sh tools/ci/package.sh uwp
```

The package lands in `out/`. The targets are `linux-amd64`, `linux-arm64`, `windows`, `uwp`, `mac-arm64` and `mac-amd64`.

The first run builds the container image, which takes a few minutes. Windows builds also download the Windows SDK into `build/deps`. UWP builds add the MSVC headers and libraries, the SDL3-uwp build and `makemsix`. UWP packaging also downloads `Microsoft.VCLibs.x64.14.00.appx` from Microsoft and puts it next to the app package, because Xbox and Store installs need it. Later runs reuse all of it.

On a Mac or an ARM64 Linux machine, run the scripts directly without `container.sh`. You need clang, CMake and Ninja there. `.github/workflows/build.yaml` shows what CI installs for those targets.

## Releases

Pushing a tag that starts with `v` (for example `v0.1.359`) builds every target and creates a draft GitHub release with all packages attached. Check the draft, then publish it on GitHub.

The release also carries the [Velopack](https://velopack.io) packages: Setup.exe, AppImage and the macOS portable zip, with their `releases.<channel>.json` feeds. Installed launchers read those feeds from the newest published releases, so publishing a draft is what ships an update. `tools/packaging/make_velopack.sh` builds them from the zip, tar.xz and app zip. It needs `vpk`, a .NET tool (`dotnet tool install -g vpk --version <VPK_VERSION>`).

## Signing

Releases are signed with two long-lived self-signed certificates, so a device or Mac sees the same publisher on every release:

- UWP: `CERT_PFX_B64` and `CERT_PASSWORD`. The release carries the public part as `onevs100-uwp.cer`. A device trusts it once.
- macOS: `MAC_CERT_P12_B64` and `MAC_CERT_PASSWORD`. The `velopack-mac` job imports the certificate into a temporary keychain (`.github/actions/mac-signing`). `vpk pack` then signs the whole bundle once, with the hardened runtime and `tools/packaging/mac/1vs100.entitlements`. Gatekeeper still blocks a self-signed app, so the quarantine step in `RUN.md` stays.

The plain macOS app zip is signed ad-hoc, since arm64 code does not run unsigned. Without the secrets, UWP gets a new test certificate. Keep the certificate files and their passwords offline. A lost UWP key means every device has to trust a new certificate.

## Pins

`versions.env` holds every toolchain version. Changing it rebuilds the container image and the CI caches.

The SDL3-uwp build comes from a release of [1vs100/SDL3-uwp](https://github.com/1vs100/SDL3-uwp), named in `tools/uwp/sdl3-uwp-prefix.txt`. See `tools/uwp/README.md` for why.
