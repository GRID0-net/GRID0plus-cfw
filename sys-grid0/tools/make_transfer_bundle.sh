#!/usr/bin/env bash
set -euo pipefail

project_dir="$(cd "$(dirname "$0")/.." && pwd)"
archive_name="sys-GRID+-dev-transfer-2026-08-29.tgz"
archive_path="$project_dir/$archive_name"
temporary_archive="$(mktemp --suffix=.tgz)"
trap 'rm -f "$temporary_archive"' EXIT

cd "$project_dir/.."

tar -czf "$temporary_archive" \
    --exclude='sys-GRID+/.git' \
    --exclude='sys-GRID+/.agents' \
    --exclude='sys-GRID+/.codex' \
    --exclude='sys-GRID+/.research-splatoon' \
    --exclude='sys-GRID+/.szt-transfer.tgz' \
    --exclude='sys-GRID+/sys-GRID+-dev-transfer-*.tgz' \
    --exclude='sys-GRID+/Atmosphere-libs/.git' \
    --exclude='sys-GRID+/Atmosphere-libs/libstratosphere/build' \
    --exclude='sys-GRID+/Atmosphere-libs/libstratosphere/lib' \
    --exclude='sys-GRID+/Atmosphere-libs/libstratosphere/include/stratosphere.hpp.gch' \
    --exclude='sys-GRID+/ZeroTierOne/.git' \
    --exclude='sys-GRID+/build' \
    --exclude='sys-GRID+/build-ztcore' \
    --exclude='sys-GRID+/build.log' \
    --exclude='sys-GRID+/sys-GRID+.elf' \
    --exclude='sys-GRID+/sys-GRID+.nso' \
    --exclude='sys-GRID+/sys-GRID+.nsp' \
    --exclude='sys-GRID+/sys-GRID+.npdm' \
    --exclude='sys-GRID+/tests/test_vnet' \
    --exclude='sys-GRID+/exefs' \
    --exclude='sys-GRID+/exefs.nsp' \
    --exclude='*.secret' \
    --exclude='*token.secret' \
    sys-GRID+

mv -f "$temporary_archive" "$archive_path"
trap - EXIT
printf '%s\n' "$archive_path"
