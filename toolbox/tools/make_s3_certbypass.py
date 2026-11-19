#!/usr/bin/env python3
"""Regenerate Splatoon 3's four TLS patches for one game build.

    make_s3_certbypass.py [--version 11.3.0|12.0.0] [operator-main.flat]

See docs/game-specific-tls-pinning.md for what each site is. In 12.0.0 every
site keeps its 11.3.0 instruction and the surrounding code: the gRPC sites stay
at the same RVAs, next to the same error strings, and the curl sites move by
0x310. Curl_pin_peer_pubkey still references "sha256//", and the csel feeds the
game's only nnsslConnectionSetVerifyOption call (+0x6e2f68).

Optionally pass the operator's main.flat to verify every original instruction.
The game dump is never copied into the repository.
"""
import argparse
from pathlib import Path
import subprocess
import sys

PATCHES = {
    # patch name: [(rva per build, original, replacement)]
    "s3certpin_bypass": [("certpin", "ffc301d1fd7b01a9", "00008052c0035fd6")],  # mov w0, #0; ret
    "s3verifyoption_bypass": [("verifyoption", "41018b1a", "01008052")],  # mov w1, #0
    "s3grpcverify_bypass": [("grpcverify", "aae24039", "2a008052")],  # mov w10, #1
    "s3grpcpeer_bypass": [("grpcpeer_result", "f403002a", "f4031f2a"),  # mov w20, wzr
                          ("grpcpeer_name", "c0020034", "1f2003d5")],  # nop
}
BUILDS = {
    "11.3.0": ("28C4287AEE36F7499DA60F3E68B54C70DA382D75", {
        "certpin": 0x6E1F48, "verifyoption": 0x6E2C54, "grpcverify": 0x157A20,
        "grpcpeer_result": 0x14DC80, "grpcpeer_name": 0x14E0B0}),
    "12.0.0": ("6CBB16698653E1AF3C3A7602164819BB3750866B", {
        "certpin": 0x6E2258, "verifyoption": 0x6E2F64, "grpcverify": 0x157A20,
        "grpcpeer_result": 0x14DC80, "grpcpeer_name": 0x14E0B0}),
}


def main():
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument("--version", choices=BUILDS, default="11.3.0")
    parser.add_argument("main_flat", type=Path, nargs="?")
    options = parser.parse_args()
    build_id, sites = BUILDS[options.version]
    data = options.main_flat.read_bytes() if options.main_flat else None
    root = Path(__file__).resolve().parent.parent
    for name, records in PATCHES.items():
        args = []
        for site, expected, patch in records:
            rva = sites[site]
            if data is not None and data[rva:rva + len(bytes.fromhex(expected))] != bytes.fromhex(expected):
                raise SystemExit(f"Unexpected instruction at {rva:#x}; do not patch this build")
            args += [hex(rva), patch]
        output = root.parent / "patches/game/0100C2500FC20000/exefs_patches" / name
        output.mkdir(parents=True, exist_ok=True)
        first = output / f"{build_id}.ips"
        subprocess.run([sys.executable, str(root / "tools/make_exefs_ips.py"), str(first), *args], check=True)
        (output / f"{build_id}{'0' * 24}.ips").write_bytes(first.read_bytes())


if __name__ == "__main__":
    main()
