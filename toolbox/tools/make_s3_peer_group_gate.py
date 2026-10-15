#!/usr/bin/env python3
"""Reproduce the native-owner readiness records. Requires clang/lld/llvm-objcopy.

No game dump is required to assemble. make_s3_smallmatch.py can verify every
original patch span against the operator's flat text before generating IPS.
Fixed padding was checked for direct branch and ELF pointer references in the
11.3.0 image. Never use these addresses with another build.
"""
import json
from pathlib import Path
import subprocess
import tempfile

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
ldr x8, [x8, #0x8a0]
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
CAVES = [58853396, 58923156, 58927348, 58928756, 58931284, 58931668, 58935156, 58935540, 58939412, 58950036, 58950852, 58957524, 58965732, 58969876, 58982100, 58984196, 58995780, 59002404, 59003044, 59018276, 59074756, 59093204, 59095204, 59095764, 59121428, 59126452, 59127892, 59140164, 59228964, 59241428, 59249524, 59251300, 59252516, 59262884, 59265556, 59460692, 59520276, 59529220, 59544692, 59573140, 59626532, 59627172, 59650820, 59655124, 59664036, 59722596, 59777956, 59798068]
NATIVES = {'lock_mutex': 82362160, 'unlock_mutex': 82362176, 'weak_lock': 82362768, 'get_group': 4204764, 'release_weak': 82359136, 'wait_more': 59392596, 'wait_enough': 59392532, 'root_page': 102633472}

def records():
    lines, labels = [], {}
    for line in ASSEMBLY.strip().splitlines():
        if line.endswith(':'):
            labels.setdefault(len(lines), []).append(line)
        else:
            lines.append(line)
    assert (len(lines) + 1) // 2 == len(CAVES)
    asm = ['.text']
    for index, address in enumerate(CAVES):
        asm += [f'.org {address}', f'cave_{index}:']
        for slot in range(2):
            pos = index * 2 + slot
            asm.extend(labels.get(pos, []))
            asm.append(lines[pos] if pos < len(lines) else 'nop')
        asm.append(f'b cave_{index + 1}' if index + 1 < len(CAVES) else 'nop')
    with tempfile.TemporaryDirectory(prefix='grid0-readiness-') as folder:
        src, elf, raw = [Path(folder) / name for name in ('gate.s', 'gate.elf', 'gate.bin')]
        src.write_text('\n'.join(asm) + '\n')
        symbols = [f'-Wl,--defsym={name}={address}' for name, address in NATIVES.items()]
        subprocess.run(['clang', '--target=aarch64-none-elf', '-nostdlib', '-fuse-ld=lld',
                        '-Wl,-Ttext=0', '-Wl,--image-base=0', '-Wl,--entry=0',
                        *symbols, str(src), '-o', str(elf)], check=True)
        subprocess.run(['llvm-objcopy', '-O', 'binary', '--only-section=.text', str(elf), str(raw)], check=True)
        image = raw.read_bytes()
    def branch(at, target, condition=None):
        d = (target - at) // 4
        word = (0x14000000 | (d & 0x3ffffff)) if condition is None else (0x54000000 | ((d & 0x7ffff) << 5) | condition)
        return word.to_bytes(4, 'little').hex()
    result = [[0x38a4210, branch(0x38a4210, 0x38a4254, 1), branch(0x38a4210, CAVES[0])]]
    return result + [[at, '00' * 12, image[at:at + 12].hex()] for at in CAVES]


if __name__ == '__main__':
    path = Path(__file__).with_name('s3_peer_group_wait_records.json')
    actual = records()
    assert actual == json.loads(path.read_text()), 'Generated records differ from the tested patch'
    print(f'{len(actual)} readiness records reproduce exactly')
