#!/usr/bin/env python3
"""Regenerate the v11.3.0 patch that keeps a player's chosen loadout (s3loadout_keep).

s3gearcheck_bypass stops the loadout validator (0x7103572df0) from dropping the
room, but the validator and the code around it still "repair" what they are
given. A Grizzco or Free weapon, or a gear piece outside the versus tables or
above the player's level, therefore reached the match as the default
(Splattershot Jr. for weapons, the starter piece for gear), and with a Grizzco
weapon the match also hit a comm error right after "Battle Time". This patch
removes those repairs and the two kicks that remain, so the player keeps what
they picked.

Weapon:
  0x710358687c  loadout builder: keep the real weapon id; only the info lookup
                in versus table 11 (0x71016b5cd0) uses 10 when it misses
  0x7103586a64  its caller: pass [x19+0x70] instead of 10
  0x71032e853c  skip the reason-7 loadout consistency kick (0x71032e8b00)
  0x7103897c50  skip the generic kick (0x71014f44cc) raised right after the
                validator flags a Grizzco loadout. UNCONFIRMED: what that kick
                guards beyond this; it may also stop legitimate kicks.
  0x7103573020  validator: a weapon missing from table 11 is not rebuilt as 10
  0x710325c4bc  match setup: same, for its own table-11 lookup
  0x71035730c0  validator: skip the whole weapon repair. A Grizzco weapon is in
                table 11 but fails the level check or the [x0+0x50] flag.

Gear:
  0x710358303c  per-piece checker (0x7103583010, for head, clothes and shoes):
                skip the "piece exists and is allowed" step that replaced the id
                with the type's default piece. Rarity and ability checks after
                it still run, and every later gear lookup there handles a miss.

Every console builds and checks its own copy of every player's loadout, so all
players in a match need this and s3gearcheck_bypass. An id the game truly has
no entry for now reaches the renderer; only pieces and weapons from the game's
own tables have been tried on hardware (2026-11-23).

Optionally pass the operator's main.flat to verify the original instructions.
The game dump is never copied into the repository.
"""
from pathlib import Path
import subprocess
import sys

BUILD_ID = "28C4287AEE36F7499DA60F3E68B54C70DA382D75"
RECORDS = [
    (0x358687C, "55018052e00316aae103152a", "e00316aa410180521f2003d5"),  # mov x0,x22; mov w1,#10; nop
    (0x3586A64, "41018052", "617240b9"),  # mov w1,#10 -> ldr w1,[x19,#0x70]
    (0x32E853C, "20080036", "41000014"),  # tbz -> b 0x71032e8640
    (0x3897C50, "e0fc0736", "e7ffff17"),  # tbz -> b 0x7103897bec
    (0x3573020, "000500b4", "800500b4"),  # cbz -> 0x71035730d0
    (0x325C4BC, "a00c00b4", "1f2003d5"),  # cbz -> nop
    (0x35730C0, "60820691", "04000014"),  # add -> b 0x71035730d0
    (0x358303C, "a3030036", "1d000014"),  # tbz w3 -> b 0x71035830b0
]


def main():
    if len(sys.argv) > 2:
        raise SystemExit("usage: make_s3_loadout_keep.py [operator-main.flat]")
    if len(sys.argv) == 2:
        data = Path(sys.argv[1]).read_bytes()
        for rva, expected, _ in RECORDS:
            if data[rva:rva + len(expected) // 2] != bytes.fromhex(expected):
                raise SystemExit(f"Unexpected instruction at {rva:#x}; do not patch this build")
    root = Path(__file__).resolve().parent.parent
    output = root.parent / "patches/game/0100C2500FC20000/exefs_patches/s3loadout_keep"
    output.mkdir(parents=True, exist_ok=True)
    args = [arg for rva, _, patch in RECORDS for arg in (hex(rva), patch)]
    first = output / f"{BUILD_ID}.ips"
    subprocess.run([sys.executable, str(root / "tools/make_exefs_ips.py"), str(first), *args], check=True)
    (output / f"{BUILD_ID}{'0' * 24}.ips").write_bytes(first.read_bytes())


if __name__ == "__main__":
    main()
