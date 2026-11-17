#!/usr/bin/env python3
"""Regenerate the v11.3.0 loadout-kick patch (s3gearcheck_bypass).

Splatoon 3 validates every player's loadout (name, weapon, the three gear
pieces, their main and sub abilities, splashtag parts) with the function at
0x7103572df0. It returns true when anything is out of range: gear or a weapon
that does not exist, a rarity outside 0-5, a main ability that piece can't
carry, a sub ability that is main-only, more sub slots than the rarity allows,
or a weapon above the player's level. It also repairs the copy it is given,
replacing each bad field with a default.

When it returns true for someone else's loadout, the caller at 0x710389c0e0
raises UserSettingValidation and drops that player, which is the comm error a
save with "illegal" gear causes for everyone in the room. For your own
loadout, 0x71032b88f0 raises MyUserSettingValidation. Neither is gated by the
schedule's anti-cheat flags (950a93c turned those off without effect).

The three branches that lead to those errors are skipped. The validator still
runs, so the repaired copy is what the room uses: an illegal ability shows as
the default to the others instead of kicking anyone, and an id the game can't
render never reaches it.

Optionally pass the operator's main.flat to verify the original instructions.
The game dump is never copied into the repository.
"""
from pathlib import Path
import subprocess
import sys

BUILD_ID = "28C4287AEE36F7499DA60F3E68B54C70DA382D75"
RECORDS = [
    (0x389C6F0, "60020037", "1f2003d5"),  # tbnz -> nop: other player, no level check
    (0x389C738, "a0120036", "95000014"),  # tbz -> b: other player, level-checked path
    (0x32B8A14, "000b0036", "58000014"),  # tbz -> b: own loadout
]


def main():
    if len(sys.argv) > 2:
        raise SystemExit("usage: make_s3_gearcheck.py [operator-main.flat]")
    if len(sys.argv) == 2:
        data = Path(sys.argv[1]).read_bytes()
        for rva, expected, _ in RECORDS:
            if data[rva:rva + 4] != bytes.fromhex(expected):
                raise SystemExit(f"Unexpected instruction at {rva:#x}; do not patch this build")
    root = Path(__file__).resolve().parent.parent
    output = root.parent / "patches/game/0100C2500FC20000/exefs_patches/s3gearcheck_bypass"
    output.mkdir(parents=True, exist_ok=True)
    args = [arg for rva, _, patch in RECORDS for arg in (hex(rva), patch)]
    first = output / f"{BUILD_ID}.ips"
    subprocess.run([sys.executable, str(root / "tools/make_exefs_ips.py"), str(first), *args], check=True)
    (output / f"{BUILD_ID}{'0' * 24}.ips").write_bytes(first.read_bytes())


if __name__ == "__main__":
    main()
