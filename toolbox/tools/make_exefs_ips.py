#!/usr/bin/env python3
"""Write an Atmosphere exefs IPS patch from a module-relative address.

    tools/make_exefs_ips.py OUT.ips RVA HEXBYTES [RVA HEXBYTES ...]

RVA is the address inside the loaded module (what Ghidra shows minus its
base, what nx2elf's ELF shows directly). Atmosphere's loader subtracts the
0x100-byte NSO header from every IPS record offset before writing
(libstratosphere patcher_api.cpp: "patch_offset -= offset", with the offset
being sizeof(NsoHeader)), so the record must say RVA + 0x100. Writing the
bare RVA -- as v0.4.0's hand-made Splatoon 3 patches did -- lands the patch
0x100 bytes early, on an unrelated instruction.
"""
import sys

NSO_HEADER_SIZE = 0x100


def main() -> None:
    if len(sys.argv) < 4 or len(sys.argv) % 2 != 0:
        sys.exit(__doc__)
    out = bytearray(b"PATCH")
    for rva_s, data_s in zip(sys.argv[2::2], sys.argv[3::2]):
        rva, data = int(rva_s, 16), bytes.fromhex(data_s)
        off = rva + NSO_HEADER_SIZE
        if off >= 1 << 24 or not 0 < len(data) < 1 << 16:
            sys.exit(f"record out of IPS range: rva={rva_s} len={len(data)}")
        out += off.to_bytes(3, "big") + len(data).to_bytes(2, "big") + data
    out += b"EOF"
    with open(sys.argv[1], "wb") as f:
        f.write(out)


if __name__ == "__main__":
    main()
