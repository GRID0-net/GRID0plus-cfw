#!/usr/bin/env python3
"""Regenerate the Splatoon 3 undersized-match patch from independently checked RVAs.

    make_s3_smallmatch.py [--version 11.3.0|12.0.0] [operator-main.flat]

Optionally pass the operator's main.flat to verify the original instructions.
The game dump is never copied into the repository. Atmosphere offsets include
its 0x100-byte NSO header; the generic builder selects IPS32 for these RVAs.
"""
import argparse
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


def wait_records(name):
    return [tuple(row) for row in json.loads(Path(__file__).with_name(name).read_text())]


WAIT_RECORDS = wait_records("s3_peer_group_wait_records.json")
RECORDS += WAIT_RECORDS
# Keep every start route in this retained-lobby controller behind the server's
# backfill lock, rather than increasing its 120-update player-stability timer.
RECORDS += [(0x389F654, instruction(branch(0x389F654, 0x389F798, condition=11)), instruction(branch(0x389F654, A)))]
RECORDS += [(rva, "00" * 12, "".join(instruction(w) for w in words)) for rva, words in REMATCH_BLOCKS.items()]



# 12.0.0 (build 6CBB1669...). Every original instruction and branch
# destination below was checked against the 12.0.0 executable; the lobby, the
# rematch controller and the wait loop keep their 11.3.0 shape and structure
# offsets (session manager +41/+64/+73, lobby +0x214, +0x2ec).
#  - Tricolor lobby wait: 12.0.0 inverts the equality, b.ne to the "not yet"
#    path at +0x38b4028 (cmp count, 1; b.lt; cmp count, required; b.ge), with
#    the "count == required" path falling through. b.ne becomes b.gt, so one
#    or two players take the fall-through, the same set 11.3.0's b.le admits.
#  - Rematch: the controller's b.lt is at +0x38a8254; its destinations are
#    +0x38a839c (lt) and +0x38a8258 (fall-through), and the lobby wait the
#    gate jumps to is +0x38a8738 (fcvtzs w22, s8; 11.3.0 used w21). Live-in
#    registers at all three are x19, x23-x28 and d8-d15 in both builds, none of
#    which the skipped code writes, and x8/x16 are dead there.
#  - Padding A, B, C, D: three UDF words each, unreferenced (see the
#    readiness generator for the checks), within +-1 MiB of every conditional
#    target. C is a new span; 11.3.0's C has no 12.0.0 counterpart.
A12, B12, C12, D12 = 0x3899AD4, 0x3899E54, 0x38A3214, 0x38AB674
WAIT12, LT12, NEXT12, GATE12 = 0x38A8738, 0x38A839C, 0x38A8258, 0x38A8254
REMATCH_BLOCKS_12 = {
    A12: [0xf9400308, 0x3940a510, branch(A12+8, B12)],
    B12: [branch(B12, WAIT12, register=16), 0x39410110, branch(B12+8, D12)],
    D12: [branch(D12, C12+4, register=16, zero=True), 0x39412508, branch(D12+8, C12)],
    C12: [branch(C12, WAIT12, register=8), branch(C12+4, LT12, condition=11), branch(C12+8, NEXT12)],
}
RECORDS_12 = [
    (0x38AD098, "1f00186b", "1f00006b"),  # WaitFullmember
    (0x32C375C, "9f02006b", "1f00006b"),  # MatchSyncDisconnect1
    (0x389F6C0, "bf02086b", "1f01086b"),  # ShareMemberNumInvalid3
    (0x38A8350, "1f00086b", "1f080071"),  # lobby minimum
    (0x38A8468, "1f00086b", "1f080071"),  # lobby minimum
    (0x38B3F30, "c1070054", "cc070054"),  # tricolor lobby wait: b.ne -> b.gt
    (0x38B83A4, "80010054", "8d010054"),  # tricolor delegation: b.eq -> b.le
]
RECORDS_12 += wait_records("s3_peer_group_wait_records-12.0.0.json")
RECORDS_12 += [(GATE12, instruction(branch(GATE12, LT12, condition=11)), instruction(branch(GATE12, A12)))]
RECORDS_12 += [(rva, "00" * 12, "".join(instruction(w) for w in words)) for rva, words in REMATCH_BLOCKS_12.items()]

BUILDS = {
    "11.3.0": (BUILD_ID, RECORDS),
    "12.0.0": ("6CBB16698653E1AF3C3A7602164819BB3750866B", RECORDS_12),
}
# Emulator tests read these per build.
REMATCH = {
    "11.3.0": {"gate": 0x389F654, "wait": WAIT, "lt": 0x389F798, "next": 0x389F658, "manager_slot": 0x605C278},
    "12.0.0": {"gate": GATE12, "wait": WAIT12, "lt": LT12, "next": NEXT12, "manager_slot": 0x606B278},
}


def main():
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument("--version", choices=BUILDS, default="11.3.0")
    parser.add_argument("main_flat", type=Path, nargs="?")
    options = parser.parse_args()
    build_id, records = BUILDS[options.version]
    spans = sorted((rva, rva + len(bytes.fromhex(patch))) for rva, _, patch in records)
    assert all(end <= start for (_, end), (start, _) in zip(spans, spans[1:])), "overlapping records"
    if options.main_flat:
        data = options.main_flat.read_bytes()
        for rva, expected, _ in records:
            if data[rva:rva + len(bytes.fromhex(expected))] != bytes.fromhex(expected):
                raise SystemExit(f"Unexpected instruction at {rva:#x}; do not patch this build")
    root = Path(__file__).resolve().parent.parent
    output = root.parent / "patches/game/0100C2500FC20000/exefs_patches/s3smallmatch_bypass"
    output.mkdir(parents=True, exist_ok=True)
    args = [arg for rva, _, patch in records for arg in (hex(rva), patch)]
    first = output / f"{build_id}.ips"
    subprocess.run([sys.executable, str(root / "tools/make_exefs_ips.py"), str(first), *args], check=True)
    (output / f"{build_id}{'0' * 24}.ips").write_bytes(first.read_bytes())


if __name__ == "__main__":
    main()
