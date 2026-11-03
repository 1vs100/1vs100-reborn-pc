#!/usr/bin/env python3
"""Build package/manifest.txt from manifest-kinds.txt and reference files.

Usage:
  tools/uwp/make_manifest.py --store <dir> --kinds tools/uwp/manifest-kinds.txt \
      --out package/manifest.txt

<dir> mirrors the user-data root: content/<packs> and
avatar-content/0000000000000000/... . Each kinds line is
"<kind> <store-relative path>"; the script hashes the file at
<dir>/<path> and writes "<sha256-hex> <size> <kind> <path>" per line.

Kinds: game, launch, show, episode, avatar. The UWP onboarding embeds
this file and verifies downloads against it; the desktop launcher reads
it for its Files tab (exact name + size + role).
"""
import argparse
import hashlib
import sys
from pathlib import Path

KINDS = {"game", "launch", "show", "episode", "avatar"}


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--store", required=True, help="dir mirroring the user-data root")
    parser.add_argument("--kinds", required=True, help="manifest-kinds.txt")
    parser.add_argument("--out", required=True, help="manifest.txt to write")
    args = parser.parse_args()

    store = Path(args.store)
    lines = []
    for lineno, raw in enumerate(Path(args.kinds).read_text().splitlines(), 1):
        line = raw.strip()
        if not line or line.startswith("#"):
            continue
        kind, _, path = line.partition(" ")
        kind, path = kind.strip(), path.strip()
        if kind not in KINDS or not path or ".." in path or path.startswith("/"):
            print(f"kinds:{lineno}: bad line: {raw!r}", file=sys.stderr)
            return 1
        if "/" not in path and kind == "avatar":
            print(f"kinds:{lineno}: avatar entries need a relative path: {raw!r}", file=sys.stderr)
            return 1
        source = store / path
        if not source.is_file():
            print(f"kinds:{lineno}: missing file: {source}", file=sys.stderr)
            return 1
        digest = hashlib.sha256()
        size = 0
        with open(source, "rb") as f:
            while chunk := f.read(1 << 20):
                digest.update(chunk)
                size += len(chunk)
        lines.append(f"{digest.hexdigest()} {size} {kind} {path}\n")

    out = Path(args.out)
    out.write_text("".join(lines))
    print(f"wrote {len(lines)} entries to {out}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
