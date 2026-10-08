#!/usr/bin/env python3
"""Regenerate the v11.3.0 undersized-match patch from independently checked RVAs.

Optionally pass the operator's main.flat to verify the original instructions.
The game dump is never copied into the repository. Atmosphere offsets include
its 0x100-byte NSO header; the generic builder selects IPS32 for these RVAs.
"""
from pathlib import Path
import subprocess
import sys

BUILD_ID = "28C4287AEE36F7499DA60F3E68B54C70DA382D75"
# Original comparisons, replacement comparisons. Branches remain untouched.
RECORDS = [
    (0x38A420C, "1f00186b", "1f00006b"),  # WaitFullmember
    (0x32C1C7C, "9f02006b", "1f00006b"),  # MatchSyncDisconnect1
    (0x3898160, "bf02086b", "1f01086b"),  # ShareMemberNumInvalid3
    # This lobby controller defaults its minimum to eight at main+0x389f4b4.
    # Lower both minimum comparisons to two, not the stable-count comparison
    # at +0x389f75c or the ready/current-count equality at +0x389f8b4.
    (0x389F750, "1f00086b", "1f080071"),
    (0x389F864, "1f00086b", "1f080071"),
]


def main():
    if len(sys.argv) > 2:
        raise SystemExit("usage: make_s3_smallmatch.py [operator-main.flat]")
    if len(sys.argv) == 2:
        data = Path(sys.argv[1]).read_bytes()
        for rva, expected, _ in RECORDS:
            if data[rva:rva + 4] != bytes.fromhex(expected):
                raise SystemExit(f"Unexpected instruction at {rva:#x}; do not patch this build")
    root = Path(__file__).resolve().parent.parent
    output = root / "romfs/sd/atmosphere/exefs_patches/s3smallmatch_bypass"
    output.mkdir(parents=True, exist_ok=True)
    args = [arg for rva, _, patch in RECORDS for arg in (hex(rva), patch)]
    first = output / f"{BUILD_ID}.ips"
    subprocess.run([sys.executable, str(root / "tools/make_exefs_ips.py"), str(first), *args], check=True)
    (output / f"{BUILD_ID}{'0' * 24}.ips").write_bytes(first.read_bytes())


if __name__ == "__main__":
    main()
