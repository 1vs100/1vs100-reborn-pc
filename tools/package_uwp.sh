#!/usr/bin/env bash
# Builds a signed .appx of the Windows Store build for sideloading.
#
# Usage: package_uwp.sh <build_dir> <out_dir>
#
# Environment:
#   MAKEMSIX       makemsix built with MSIX_PACK=on (default: makemsix on PATH)
#   VCLIBS_APPX    Microsoft.VCLibs.x64.14.00.appx, copied next to the package
#   CERT_PFX       signing certificate; its subject must equal the manifest
#                  Publisher. When unset, a test certificate is created once in
#                  ~/.local/share/onevs100-uwp-cert and reused.
#   CERT_PASSWORD  password of CERT_PFX (default: none)
#
# Game files are NOT shipped in the package. On first run the app asks for
# them (browser upload or USB) and verifies them against package/manifest.txt.
# The package contains no game files; they are fetched on first run.
set -euo pipefail

if [ $# -ne 2 ]; then
  echo "usage: $0 <build_dir> <out_dir>" >&2
  exit 2
fi
build_dir=$(realpath "$1")
out_dir=$(realpath -m "$2")
script_dir=$(dirname "$(realpath "$0")")
makemsix=${MAKEMSIX:-makemsix}

version_file="$script_dir/../VERSION"
if [ ! -f "$version_file" ]; then
  echo "missing $version_file" >&2
  exit 1
fi
version=$(tr -d '[:space:]' < "$version_file")
if ! [[ "$version" =~ ^[0-9]+(\.[0-9]+){3}$ ]]; then
  echo "bad version $version, want four numeric parts" >&2
  exit 1
fi
for f in onevs100.exe rexruntime.dll rexgpu-xenos.dll; do
  if [ ! -f "$build_dir/$f" ]; then
    echo "missing $build_dir/$f" >&2
    exit 1
  fi
done

layout="$out_dir/layout"
rm -rf "$layout"
mkdir -p "$layout/Logos"
cp "$script_dir/uwp/AppxManifest.xml" "$layout/AppxManifest.xml"
sed -i "s/__VERSION__/$version/" "$layout/AppxManifest.xml"
if grep -q "__VERSION__" "$layout/AppxManifest.xml"; then
  echo "version stamp leaked into AppxManifest.xml" >&2
  exit 1
fi

cp "$build_dir"/onevs100.exe "$build_dir"/rexruntime.dll "$build_dir"/rexgpu-xenos.dll \
  "$build_dir"/SDL3.dll "$layout/"
# The native renderer translates shader microcode at run time, so the compiler
# ships in the package; a packaged app cannot load these from the system.
if [ -f "$build_dir/dxcompiler.dll" ]; then
  cp "$build_dir"/dxcompiler.dll "$build_dir"/dxil.dll "$layout/"
fi
# Same config as the desktop archives; copied to LocalState on first run.
cp "$script_dir/../package/onevs100.toml" "$layout/"

logo() {
  magick -size "$2" xc:'#1a237e' -fill white -gravity center \
    -pointsize "$3" -annotate 0 '1 vs 100' "$layout/Logos/$1"
}
logo StoreLogo.png 50x50 10
logo Square44x44Logo.png 44x44 9
logo Square150x150Logo.png 150x150 30
logo SplashScreen.png 620x300 72

# Reusing one test certificate means the device only has to trust it once.
cert_dir="$HOME/.local/share/onevs100-uwp-cert"
cert=${CERT_PFX:-$cert_dir/test-cert.pfx}
cert_password=${CERT_PASSWORD:-}
if [ -z "${CERT_PFX:-}" ] && [ ! -f "$cert" ]; then
  mkdir -p "$cert_dir"
  chmod 700 "$cert_dir"
  (umask 077
   openssl req -x509 -newkey rsa:2048 -nodes -days 3650 -subj "/CN=OneVs100 Test" \
     -addext "extendedKeyUsage=codeSigning" -addext "basicConstraints=CA:FALSE" \
     -addext "keyUsage=digitalSignature" \
     -keyout "$cert_dir/test-cert.key" -out "$cert_dir/test-cert.cer" 2>/dev/null
   openssl pkcs12 -export -inkey "$cert_dir/test-cert.key" -in "$cert_dir/test-cert.cer" \
     -out "$cert" -passout pass:)
fi

# Installation fails unless the certificate subject equals Identity/@Publisher.
publisher=$(sed -n 's/.*<Identity [^>]*Publisher="\([^"]*\)".*/\1/p' "$layout/AppxManifest.xml")
subject=$(openssl pkcs12 -in "$cert" -passin "pass:$cert_password" -nokeys -clcerts 2>/dev/null \
  | openssl x509 -noout -subject | sed 's/^subject= *//')
if [ "$subject" != "$publisher" ]; then
  echo "certificate subject '$subject' does not match manifest Publisher '$publisher'" >&2
  exit 1
fi

unsigned="$out_dir/onevs100-unsigned.appx"
signed="$out_dir/onevs100.appx"
rm -f "$unsigned" "$signed"
"$makemsix" pack -d "$layout" -p "$unsigned"
osslsigncode sign -pkcs12 "$cert" -pass "$cert_password" -in "$unsigned" -out "$signed"
rm -f "$unsigned"

if [ -n "${VCLIBS_APPX:-}" ]; then
  cp "$VCLIBS_APPX" "$out_dir/"
fi

echo "$signed"
