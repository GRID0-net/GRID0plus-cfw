#!/usr/bin/env bash
# Publish patches/ as the GitHub release patches-<serial>.
#
# Consoles and emulators install patches only from these releases, so this is
# the one deliberate step that ships a patch: merging to main does not.
#
# The release is created with --latest=false. The toolbox's own self-updater
# reads GitHub's "latest release", and a patches release marked latest would
# hide every toolbox update behind it.
set -euo pipefail

cd "$(dirname "$0")/.."
REPO=GRID0-net/GRID0plus-cfw

python3 -I tools/patches_manifest.py --check

if [ -n "$(git status --porcelain -- patches tools/patches_manifest.py)" ]; then
    echo "patches/ has uncommitted changes; commit them first" >&2
    exit 1
fi
git fetch -q origin main
if ! git merge-base --is-ancestor HEAD origin/main; then
    echo "HEAD is not on origin/main; push it first" >&2
    exit 1
fi

SERIAL=$(python3 -I -c 'import json; print(json.load(open("patches/manifest.json"))["serial"])')
TAG="patches-${SERIAL}"
if gh release view "$TAG" -R "$REPO" >/dev/null 2>&1; then
    echo "$TAG already exists; change patches/ and regenerate the manifest to publish a new serial" >&2
    exit 1
fi

STAGE=$(mktemp -d)
trap 'rm -rf "$STAGE"' EXIT
cp patches/manifest.json "$STAGE/"
# One asset per distinct file, named by its hash, checked again on the way out.
python3 -I - "$STAGE" <<'EOF'
import hashlib, json, pathlib, shutil, sys
stage = pathlib.Path(sys.argv[1])
for p in json.load(open("patches/manifest.json"))["patches"]:
    src = pathlib.Path("patches") / (
        f"game/{p['title_id']}/{p['dest']}" if p["kind"] == "game" else f"system/{p['dest']}")
    if hashlib.sha256(src.read_bytes()).hexdigest() != p["sha256"]:
        sys.exit(f"{src}: hash does not match the manifest")
    shutil.copyfile(src, stage / p["asset"])
EOF

echo "Publishing $TAG from $(git rev-parse --short HEAD) with $(ls "$STAGE" | wc -l) files"
gh release create "$TAG" -R "$REPO" --target "$(git rev-parse HEAD)" --latest=false \
    --title "Patches ${SERIAL}" \
    --notes "IPS patches, serial ${SERIAL}. Installed by the GRID0+ Toolbox and the GRID0+ emulators; see patches/README.md." \
    "$STAGE"/*
