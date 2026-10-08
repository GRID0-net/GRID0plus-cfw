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


def instruction(word):
    return word.to_bytes(4, "little").hex()


def branch(at, target, condition=None, register=None):
    displacement = (target - at) // 4
    if register is not None:
        assert -(1 << 18) <= displacement < (1 << 18)
        return 0x35000000 | ((displacement & 0x7ffff) << 5) | register
    if condition is not None:
        assert -(1 << 18) <= displacement < (1 << 18)
        return 0x54000000 | ((displacement & 0x7ffff) << 5) | condition
    assert -(1 << 25) <= displacement < (1 << 25)
    return 0x14000000 | (displacement & 0x3ffffff)


# Compiler alignment after terminal instructions; all four spans are UDF,
# have no direct branch or pointer references in this build, and precede
# aligned function entries. No executable is redistributed by this tool.
# The hook replaces the existing b.lt after cmp w8,#1. Loads/CBNZ/B preserve
# NZCV, so the final trampoline retains that comparison and both destinations.
# x8 is dead at all three destinations; w16 is scratch at this entry.
A, B, C, D = 0x3892594, 0x3892914, 0x389B4A4, 0x38A2804
REMATCH_BLOCKS = {
    A: [0xf9400308, 0x39410110, branch(A+8, B)], # [x24] manager, secondary active
    B: [branch(B, D, register=16), 0x3940a508, branch(B+8, C)], # primary ebf +41
    D: [0x39412508, branch(D+4, C), 0xd503201f], # secondary ebf +73
    C: [branch(C, 0x389FB2C, register=8), branch(C+4, 0x389F798, condition=11), branch(C+8, 0x389F658)],
}
# Keep every start route in this retained-lobby controller behind the server's
# backfill lock, rather than increasing its 120-update player-stability timer.
RECORDS += [(0x389F654, instruction(branch(0x389F654, 0x389F798, condition=11)), instruction(branch(0x389F654, A)))]
RECORDS += [(rva, "00" * 12, "".join(instruction(w) for w in words)) for rva, words in REMATCH_BLOCKS.items()]


def main():
    if len(sys.argv) > 2:
        raise SystemExit("usage: make_s3_smallmatch.py [operator-main.flat]")
    if len(sys.argv) == 2:
        data = Path(sys.argv[1]).read_bytes()
        for rva, expected, _ in RECORDS:
            if data[rva:rva + len(bytes.fromhex(expected))] != bytes.fromhex(expected):
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
