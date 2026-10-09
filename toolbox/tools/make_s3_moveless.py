#!/usr/bin/env python3
"""Regenerate the v11.3.0 idle-kick patch (s3moveless_bypass).

Splatoon 3 ends a battle for a player whose sticks, buttons and speed have not
changed for a while, raising PlayerMoveless. On GRID0+ that kicked emulator
players who were moving (2026-10-09), and idle test devices in every test
match. The check at 0x7103b09cb8 counts idle frames and raises the error when
the count reaches its limit (b.lt at +0x3b09e64); the branch becomes
unconditional, so the counter still runs but the error is never raised.

Optionally pass the operator's main.flat to verify the original instruction.
The game dump is never copied into the repository.
"""
from pathlib import Path
import subprocess
import sys

BUILD_ID = "28C4287AEE36F7499DA60F3E68B54C70DA382D75"
RECORDS = [
    (0x3B09E64, "8b010054", "0c000014"),  # b.lt -> b: never raise PlayerMoveless
]


def main():
    if len(sys.argv) > 2:
        raise SystemExit("usage: make_s3_moveless.py [operator-main.flat]")
    if len(sys.argv) == 2:
        data = Path(sys.argv[1]).read_bytes()
        for rva, expected, _ in RECORDS:
            if data[rva:rva + 4] != bytes.fromhex(expected):
                raise SystemExit(f"Unexpected instruction at {rva:#x}; do not patch this build")
    root = Path(__file__).resolve().parent.parent
    output = root / "romfs/sd/atmosphere/exefs_patches/s3moveless_bypass"
    output.mkdir(parents=True, exist_ok=True)
    args = [arg for rva, _, patch in RECORDS for arg in (hex(rva), patch)]
    first = output / f"{BUILD_ID}.ips"
    subprocess.run([sys.executable, str(root / "tools/make_exefs_ips.py"), str(first), *args], check=True)
    (output / f"{BUILD_ID}{'0' * 24}.ips").write_bytes(first.read_bytes())


if __name__ == "__main__":
    main()
