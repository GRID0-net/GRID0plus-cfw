#!/usr/bin/env python3
"""Generate or check patches/manifest.json from the patches/ tree.

The toolbox and the emulators install IPS patches from a `patches-<serial>`
GitHub release, never from a copy bundled in their own builds. This file is
what they compare against: one entry per installed file, with the SHA-256 the
client checks before moving a download into place and later compares with
what is actually on disk.

Layout, mirroring sdmc:/atmosphere/ below the kind:

    patches/game/<title id>/exefs_patches/<patch>/<build id>.ips
    patches/system/exefs_patches/<patch>/<build id>.ips
    patches/system/nro_patches/<patch>/<build id>.ips

`game` patches apply the next time that game starts; `system` patches are
HorizonOS modules and applets, which load at boot, so the console needs a
restart. Only `game` patches go to the emulators: they emulate the system
modules and never load those patches.

Release assets are flat, so each file is published as `<sha256>.ips`. That
also makes the 40- and 64-hex-digit names of one patch, which carry the same
bytes, a single download.

    patches_manifest.py           rewrite the manifest, bumping serial if the
                                  file set changed
    patches_manifest.py --check   exit 1 if the manifest is out of date (CI)
"""

import hashlib
import json
import pathlib
import sys

ROOT = pathlib.Path(__file__).resolve().parent.parent / "patches"
MANIFEST = ROOT / "manifest.json"
SD_ROOTS = ("exefs_patches", "nro_patches")


def scan():
    entries = []
    for path in sorted(ROOT.rglob("*.ips")):
        rel = path.relative_to(ROOT).parts
        kind = rel[0]
        if kind == "game":
            title_id, rest = rel[1].upper(), rel[2:]
            targets = ["console", "emulator"]
        elif kind == "system":
            title_id, rest = None, rel[1:]
            targets = ["console"]
        else:
            sys.exit(f"{path}: unknown kind {kind!r} (expected game or system)")
        if len(rest) != 3 or rest[0] not in SD_ROOTS:
            sys.exit(f"{path}: expected <exefs_patches|nro_patches>/<patch>/<build id>.ips")
        data = path.read_bytes()
        if not data.startswith((b"PATCH", b"IPS32")):
            sys.exit(f"{path}: not an IPS file")
        build_id = path.stem.upper()
        digest = hashlib.sha256(data).hexdigest()
        entry = {
            "name": rest[1],
            "kind": kind,
            "build_id": build_id,
            "dest": "/".join(rest),
            "size": len(data),
            "sha256": digest,
            "asset": digest + ".ips",
            "targets": targets,
        }
        if title_id:
            entry["title_id"] = title_id
        entries.append(entry)
    if not entries:
        sys.exit("no patches found")
    dests = [e["dest"] for e in entries]
    if len(set(dests)) != len(dests):
        sys.exit("two patches install to the same path")
    return entries


def main():
    check = "--check" in sys.argv[1:]
    patches = scan()
    old = json.loads(MANIFEST.read_text()) if MANIFEST.exists() else {"serial": 0, "patches": []}
    unchanged = old.get("patches") == patches
    if check:
        if not unchanged:
            sys.exit("patches/manifest.json is out of date: run tools/patches_manifest.py")
        print(f"manifest serial {old['serial']}: {len(patches)} files, up to date")
        return
    serial = old.get("serial", 0) if unchanged else old.get("serial", 0) + 1
    MANIFEST.write_text(json.dumps({"serial": serial, "patches": patches}, indent=2) + "\n")
    print(f"manifest serial {serial}: {len(patches)} files" + ("" if not unchanged else " (unchanged)"))


if __name__ == "__main__":
    main()
