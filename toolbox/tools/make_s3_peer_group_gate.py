#!/usr/bin/env python3
"""Reproduce the native-owner readiness records. Requires llvm-mc.

No game dump is required to assemble. make_s3_smallmatch.py can verify every
original patch span against the operator's flat text before generating IPS.

Every address below belongs to one build. Imported routines (mutex lock and
unlock, shared_weak_count lock and release_weak) were resolved through the
PLT stub's GOT slot to its JUMP_SLOT symbol, not by stub shape: the stubs are
identical apart from their GOT offsets. get_group and the root pointer were
matched to the 11.3.0 code, and every structure offset the gate reads was
checked against the 12.0.0 code that uses it. Each fixed padding span is
three UDF words before a 16-byte aligned entry, after a terminal instruction,
with no direct branch, ADR/ADRP pair, RELA addend or RELR pointer inside it.

    make_s3_peer_group_gate.py [--version V]   check the records reproduce
    make_s3_peer_group_gate.py --version V --write   (re)write that build's records
"""
import argparse
import json
from pathlib import Path
import re
import shutil
import subprocess

ASSEMBLY = '''
gate:
stp x0, x30, [sp, #-128]!
stp x19, x20, [sp, #16]
stp x21, x22, [sp, #32]
stp x23, x24, [sp, #48]
stp x25, x26, [sp, #64]
mov w19, w0
mov w26, w24
mov x22, xzr
mov x23, xzr
mov w25, wzr
cmp w19, #1
b.lt finish
adrp x8, root_page
ldr x8, [x8, #ROOT_OFFSET]
cbz x8, finish
ldr x20, [x8, #0x80]
cbz x20, finish
add x21, x20, #0x2d0
mov x0, x21
bl lock_mutex
ldr w8, [x20, #0x12a8]
cbz w8, unlock
cmp w8, #2
b.hi unlock
ldr w9, [x20, #0x12b8]
cmp w9, w8
csel w9, w9, wzr, lo
ldr x10, [x20, #0x12b0]
cbz x10, unlock
mov w8, #0x150
umaddl x10, w9, w8, x10
ldr w8, [x10]
cbz w8, unlock
ldr x10, [x10, #8]
cbz x10, unlock
ldr x24, [x10]
cbz x24, unlock
ldr x0, [x24, #8]
cbz x0, unlock
bl weak_lock
mov x22, x0
cbz x0, unlock
ldr x0, [x24]
cbz x0, unlock
add x8, sp, #96
bl get_group
ldp x24, x23, [sp, #96]
unlock:
mov x0, x21
bl unlock_mutex
cbz x23, finish
cbz x24, finish
ldrh w8, [x24, #0x1fa]
cmp w8, #0x101
b.ne finish
ldr w8, [x24, #0x1a8]
mov w9, #0x4106
movk w9, #0x6c6c, lsl #16
cmp w8, w9
b.ne finish
ldr x8, [x24, #0xb8]
cmp x8, #1
b.lo finish
cmp x8, x26
b.hi finish
mov w25, w8
finish:
mov x0, x23
bl release_owner
mov x0, x22
bl release_owner
cmp w19, w25
cset w8, eq
cmp w25, #0
csel w8, w8, wzr, ne
ldp x25, x26, [sp, #64]
ldp x23, x24, [sp, #48]
ldp x21, x22, [sp, #32]
ldp x19, x20, [sp, #16]
ldp x0, x30, [sp], #128
cbz w8, wait_more
b wait_enough
release_owner:
cbz x0, release_return
stp x19, x30, [sp, #-16]!
mov x19, x0
add x8, x0, #8
release_retry:
ldaxr x9, [x8]
sub x10, x9, #1
stlxr w11, x10, [x8]
cbnz w11, release_retry
cbnz x9, release_done
ldr x8, [x19]
ldr x8, [x8, #0x10]
blr x8
mov x0, x19
bl release_weak
release_done:
ldp x19, x30, [sp], #16
release_return:
ret
'''

BUILDS = {
    "11.3.0": {
        "records": "s3_peer_group_wait_records.json",
        "hook": 0x38A4210, "wait_more": 0x38A4254, "wait_enough": 0x38A4214,
        "root_offset": 0x8A0,
        "caves": [58853396, 58923156, 58927348, 58928756, 58931284, 58931668, 58935156, 58935540, 58939412, 58950036, 58950852, 58957524, 58965732, 58969876, 58982100, 58984196, 58995780, 59002404, 59003044, 59018276, 59074756, 59093204, 59095204, 59095764, 59121428, 59126452, 59127892, 59140164, 59228964, 59241428, 59249524, 59251300, 59252516, 59262884, 59265556, 59460692, 59520276, 59529220, 59544692, 59573140, 59626532, 59627172, 59650820, 59655124, 59664036, 59722596, 59777956, 59798068],
        "natives": {"lock_mutex": 82362160, "unlock_mutex": 82362176, "weak_lock": 82362768, "get_group": 4204764, "release_weak": 82359136, "root_page": 102633472},
    },
    # 12.0.0: the hook (b.ne after cmp w0, w24) and both continuations match
    # 11.3.0 exactly. PLT stubs: LockMutex 0x4e98cc0, UnlockMutex 0x4e98cd0,
    # __shared_weak_count::lock 0x4e98f20, ::__release_weak 0x4e980f0. The
    # root pointer moved from 0x61e18a0 to 0x61f3b48 (28 of 28 sampled users).
    "12.0.0": {
        "records": "s3_peer_group_wait_records-12.0.0.json",
        "hook": 0x38AD09C, "wait_more": 0x38AD0E0, "wait_enough": 0x38AD0A0,
        "root_offset": 0xB48,
        "caves": [58883204, 58952964, 58957156, 58958564, 58961092, 58961476, 58964964, 58965348, 58969668, 58979844, 58980660, 58994196, 58995540, 58999684, 59011908, 59014004, 59025588, 59032212, 59032852, 59048084, 59104564, 59123012, 59125012, 59125572, 59169972, 59156260, 59157700, 59171396, 59258788, 59271252, 59279348, 59281124, 59282340, 59292708, 59295380, 59497604, 59560116, 59569060, 59584548, 59602596, 59671924, 59672564, 59696260, 59700612, 59709524, 59768164, 59823700, 59843828],
        "natives": {"lock_mutex": 0x4E98CC0, "unlock_mutex": 0x4E98CD0, "weak_lock": 0x4E98F20, "get_group": 0x402BEC, "release_weak": 0x4E980F0, "root_page": 0x61F3000},
    },
}
CONDITIONS = {c: i for i, c in enumerate("eq ne hs lo mi pl vs vc hi ls ge lt gt le al".split())}


def llvm_mc():
    for candidate in (shutil.which("llvm-mc"), "/opt/homebrew/opt/llvm/bin/llvm-mc", "/usr/local/opt/llvm/bin/llvm-mc"):
        if candidate and Path(candidate).exists():
            return candidate
    raise SystemExit("llvm-mc not found")


def encode_plain(lines):
    """Encode label-free instructions with llvm-mc, in order."""
    result = subprocess.run([llvm_mc(), "-triple=aarch64", "-show-encoding"], input="\n".join(lines) + "\n",
                            capture_output=True, text=True, check=True).stdout
    words = [bytes(int(b, 16) for b in m.split(",")).hex() for m in re.findall(r"encoding: \[([^\]]+)\]", result)]
    assert len(words) == len(lines), (len(words), len(lines))
    return words


def encode_branch(line, at, labels):
    """Encode the label-relative forms the gate uses: b, bl, b.cond, cbz/cbnz, adrp."""
    op, _, rest = line.partition(" ")
    if not (op in ("b", "bl", "cbz", "cbnz", "adrp") or op.startswith("b.")):
        return None
    args = [a.strip() for a in rest.split(",")]
    target = labels[args[-1]]
    delta = (target - at) // 4
    if op in ("b", "bl"):
        assert -(1 << 25) <= delta < (1 << 25)
        word = (0x94000000 if op == "bl" else 0x14000000) | (delta & 0x3FFFFFF)
    elif op.startswith("b."):
        assert -(1 << 18) <= delta < (1 << 18)
        word = 0x54000000 | ((delta & 0x7FFFF) << 5) | CONDITIONS[op[2:]]
    elif op in ("cbz", "cbnz"):
        assert -(1 << 18) <= delta < (1 << 18)
        reg = args[0]
        word = (0x80000000 if reg[0] == "x" else 0) | (0x35000000 if op == "cbnz" else 0x34000000)
        word |= ((delta & 0x7FFFF) << 5) | int(reg[1:])
    elif op == "adrp":
        page = ((target & ~0xFFF) - (at & ~0xFFF)) >> 12
        assert -(1 << 20) <= page < (1 << 20)
        word = 0x90000000 | ((page & 3) << 29) | (((page >> 2) & 0x7FFFF) << 5) | int(args[0][1:])
    else:
        return None
    return word.to_bytes(4, "little").hex()


def records(version):
    build = BUILDS[version]
    caves, natives = build["caves"], dict(build["natives"])
    natives.update(wait_more=build["wait_more"], wait_enough=build["wait_enough"])
    lines, labels = [], {}
    for line in ASSEMBLY.strip().replace("#ROOT_OFFSET", hex(build["root_offset"])).splitlines():
        if line.endswith(":"):
            labels.setdefault(len(lines), []).append(line[:-1])
        else:
            lines.append(line)
    assert (len(lines) + 1) // 2 == len(caves)
    # Same layout as before: two gate instructions per cave, then a branch to the next.
    placed = []
    for index, address in enumerate(caves):
        for slot in range(2):
            pos = index * 2 + slot
            placed.append((address + 4 * slot, lines[pos] if pos < len(lines) else "nop", labels.get(pos, [])))
        placed.append((address + 8, f"b cave_{index + 1}" if index + 1 < len(caves) else "nop", []))
    symbols = dict(natives)
    symbols.update({f"cave_{i}": at for i, at in enumerate(caves)})
    for at, _, names in placed:
        symbols.update({name: at for name in names})
    branch = {at: encode_branch(text, at, symbols) for at, text, _ in placed}
    plain = [text for at, text, _ in placed if branch[at] is None]
    encoded = iter(encode_plain(plain))
    image = {at: branch[at] or next(encoded) for at, _, _ in placed}
    def jump(at, target, condition=None):
        d = (target - at) // 4
        word = (0x14000000 | (d & 0x3ffffff)) if condition is None else (0x54000000 | ((d & 0x7ffff) << 5) | condition)
        return word.to_bytes(4, 'little').hex()
    hook = build["hook"]
    result = [[hook, jump(hook, build["wait_more"], 1), jump(hook, caves[0])]]
    return result + [[at, '00' * 12, image[at] + image[at + 4] + image[at + 8]] for at in caves]


def main():
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument("--version", choices=BUILDS)
    parser.add_argument("--write", action="store_true")
    options = parser.parse_args()
    for version in [options.version] if options.version else list(BUILDS):
        path = Path(__file__).with_name(BUILDS[version]["records"])
        actual = records(version)
        if options.write:
            path.write_text(json.dumps(actual, indent=2) + "\n")
        assert actual == json.loads(path.read_text()), f"{version}: generated records differ from the tested patch"
        print(f"{version}: {len(actual)} readiness records reproduce exactly")


if __name__ == '__main__':
    main()
