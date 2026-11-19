#!/usr/bin/env python3
"""Regenerate the Splatoon 3 idle-kick patch (s3moveless_bypass).

Splatoon 3 ends a battle for a player whose sticks, buttons and speed have not
changed for a while, raising PlayerMoveless. On GRID0+ that kicked emulator
players who were moving (2026-10-09), and idle test devices in every test
match. The check at 0x7103b09cb8 counts idle frames and raises the error when
the count reaches its limit (b.lt at +0x3b09e64); the branch becomes
unconditional, so the counter still runs but the error is never raised.

Optionally pass the operator's main.flat to verify the original instruction.
The game dump is never copied into the repository.
"""
import argparse
from pathlib import Path
import subprocess
import sys

BUILD_ID = "28C4287AEE36F7499DA60F3E68B54C70DA382D75"
RECORDS = [
    (0x3B09E64, "8b010054", "0c000014"),  # b.lt -> b: never raise PlayerMoveless
]


BUILDS = {
    "11.3.0": (BUILD_ID, RECORDS),
    "12.0.0": ("6CBB16698653E1AF3C3A7602164819BB3750866B", [
    (0x3B169F4, "ab010054", "0d000014"),
    ]),
}


def main():
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument("--version", choices=BUILDS, default="11.3.0")
    parser.add_argument("main_flat", type=Path, nargs="?")
    options = parser.parse_args()
    build_id, records = BUILDS[options.version]
    if options.main_flat:
        data = options.main_flat.read_bytes()
        for rva, expected, _ in records:
            if data[rva:rva + 4] != bytes.fromhex(expected):
                raise SystemExit(f"Unexpected instruction at {rva:#x}; do not patch this build")
    root = Path(__file__).resolve().parent.parent
    output = root.parent / "patches/game/0100C2500FC20000/exefs_patches/s3moveless_bypass"
    output.mkdir(parents=True, exist_ok=True)
    args = [arg for rva, _, patch in records for arg in (hex(rva), patch)]
    first = output / f"{build_id}.ips"
    subprocess.run([sys.executable, str(root / "tools/make_exefs_ips.py"), str(first), *args], check=True)
    (output / f"{build_id}{'0' * 24}.ips").write_bytes(first.read_bytes())


if __name__ == "__main__":
    main()
