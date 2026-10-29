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
#   CONFIG_TOML    onevs100.toml to ship; copied to LocalState on first run
#   CONTENT_DIR    folder of original content packages (the game package and
#                  any show and episode packages), shipped as content/
#   ASSETS_DIR     unpacked game folder, shipped as assets/; only needed when
#                  CONTENT_DIR does not hold the game package
#   AVATAR_TOC     AvatarAssetPack.toc, shipped under avatar/ for native_avatar_content
#
# The package contains game data. Keep it private.
set -euo pipefail

if [ $# -ne 2 ]; then
  echo "usage: $0 <build_dir> <out_dir>" >&2
  exit 2
fi
build_dir=$(realpath "$1")
out_dir=$(realpath -m "$2")
script_dir=$(dirname "$(realpath "$0")")
makemsix=${MAKEMSIX:-makemsix}

for f in onevs100.exe rexruntime.dll rexgpu-xenos.dll; do
  if [ ! -f "$build_dir/$f" ]; then
    echo "missing $build_dir/$f" >&2
    exit 1
  fi
done

layout="$out_dir/layout"
rm -rf "$layout"
mkdir -p "$layout/Logos"

cp "$build_dir"/onevs100.exe "$build_dir"/rexruntime.dll "$build_dir"/rexgpu-xenos.dll \
  "$build_dir"/SDL3.dll "$layout/"
cp "$script_dir/uwp/AppxManifest.xml" "$layout/"
if [ -n "${ASSETS_DIR:-}" ]; then
  cp -r "$ASSETS_DIR" "$layout/assets"
fi
if [ -n "${CONFIG_TOML:-}" ]; then
  cp "$CONFIG_TOML" "$layout/onevs100.toml"
fi
if [ -n "${AVATAR_TOC:-}" ]; then
  toc_dir="$layout/avatar/0000000000000000/FFFE07DF/00008000/FFFE07DF00000002"
  mkdir -p "$toc_dir"
  cp "$AVATAR_TOC" "$toc_dir/AvatarAssetPack.toc"
fi
if [ -n "${CONTENT_DIR:-}" ]; then
  mkdir -p "$layout/content"
  # Package files are named by content hash and have no extension.
  find "$CONTENT_DIR" -maxdepth 1 -type f ! -name "*.*" -exec cp {} "$layout/content/" \;
fi

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
