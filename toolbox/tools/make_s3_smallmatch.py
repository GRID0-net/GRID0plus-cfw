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


def branch(at, target, condition=None, register=None, zero=False):
    displacement = (target - at) // 4
    if register is not None:
        assert -(1 << 18) <= displacement < (1 << 18)
        return (0x34000000 if zero else 0x35000000) | ((displacement & 0x7ffff) << 5) | register
    if condition is not None:
        assert -(1 << 18) <= displacement < (1 << 18)
        return 0x54000000 | ((displacement & 0x7ffff) << 5) | condition
    assert -(1 << 25) <= displacement < (1 << 25)
    return 0x14000000 | (displacement & 0x3ffffff)
# Compiler alignment after terminal instructions; all four spans are UDF,
# have no direct branch or pointer references in this build, and precede
# aligned function entries. No executable is redistributed by this tool.
# The hook replaces the existing b.lt after cmp w8,#1. Loads/CBZ/CBNZ/B
# preserve NZCV, so the final trampoline retains that comparison and both
# destinations. x8 is dead at all three destinations; w16 is scratch here.
#
# The lobby waits while EITHER live Gamesync session has backfill open:
# primary ebf (+41), or secondary ebf (+73) when the secondary is active
# (+64). 0.8.2 read only the secondary whenever one was active. A Salmon Run
# crew from a friends room holds two sessions, the room and the job, and the
# room's ebf is never opened (grid0, 2026-10-17: it writes cp true, ebf
# false after every job). So the gate read the room, passed, and the host
# locked the reopened job 2.03 s -- the 120-update stability count -- after
# its last Again! write, in all three of that evening's rematches, while
# every rematch without a room waited for GRID0's lock.
A, B, C, D = 0x3892594, 0x3892914, 0x389B4A4, 0x38A2804
WAIT = 0x389FB2C
REMATCH_BLOCKS = {
    A: [0xf9400308, 0x3940a510, branch(A+8, B)],  # [x24] manager, primary ebf +41
    B: [branch(B, WAIT, register=16), 0x39410110, branch(B+8, D)],  # open: wait; secondary active +64
    D: [branch(D, C+4, register=16, zero=True), 0x39412508, branch(D+8, C)],  # inactive: original branch; secondary ebf +73
    C: [branch(C, WAIT, register=8), branch(C+4, 0x389F798, condition=11), branch(C+8, 0x389F658)],
}
# Tricolor's first step (fest_match_tricolor_team_config, match kind 8) gathers
# a pair of one fest team and waits for exactly two players, first in the
# lobby wait and again in the delegation step that hands the pair on to the
# oneshot queue (MatchDelegateDisconnect otherwise). A team with one player
# never advances. Both equality branches become less-or-equal, so one or two
# players pass and three still do not; other kinds compare against four the
# same way. Confirmed on hardware with one player per team, 2026-10-09.
RECORDS += [
    (0x38AAC84, "e0040054", "ed040054"),  # lobby wait: count == required
    (0x38AF188, "80010054", "8d010054"),  # delegation: count == required
]
# WaitFullmember must compare connected Pia stations with the completed All
# peer-group roster, not the full room size or lobby+0x490. The latter stayed
# zero on hardware and let the Switch start alone across networks. This gate
# acquires native API/group ownership under the session manager's mutex and
# retains the game's original stability counter. Validated with a three-player
# Switch-hosted hotspot battle on 2026-10-15; all three referee profiles arrived.
# The companion generator contains readable ARM64 and reproduces these records.
import json
WAIT_HOOK, WAIT_MORE, WAIT_ENOUGH = 0x38A4210, 0x38A4254, 0x38A4214
WAIT_RECORDS = [tuple(row) for row in json.loads(
    Path(__file__).with_name("s3_peer_group_wait_records.json").read_text())]
RECORDS += WAIT_RECORDS
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
    output = root.parent / "patches/game/0100C2500FC20000/exefs_patches/s3smallmatch_bypass"
    output.mkdir(parents=True, exist_ok=True)
    args = [arg for rva, _, patch in RECORDS for arg in (hex(rva), patch)]
    first = output / f"{BUILD_ID}.ips"
    subprocess.run([sys.executable, str(root / "tools/make_exefs_ips.py"), str(first), *args], check=True)
    (output / f"{BUILD_ID}{'0' * 24}.ips").write_bytes(first.read_bytes())


if __name__ == "__main__":
    main()
