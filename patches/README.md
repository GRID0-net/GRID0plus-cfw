# IPS patches

The single source of every IPS patch GRID0+ installs: on a console through the
Toolbox, and in the GRID0+ Ryujinx and Citron builds. None of them bundle a
copy. Each one checks the newest `patches-<serial>` release when it starts,
compares the SHA-256 of what is installed against `manifest.json`, and
downloads whatever differs.

## Layout

```
game/<title id>/exefs_patches/<patch>/<build id>.ips   applies at the next game launch
system/exefs_patches/<patch>/<build id>.ips            HorizonOS module: console restart
system/nro_patches/<patch>/<build id>.ips              HorizonOS applet: console restart
```

Below the kind, paths mirror `sdmc:/atmosphere/`. `game` patches also go to
the emulators, into their own per-game mod folder. `system` patches are
console-only: the emulators emulate those modules instead.

A patch for a game build ships under both Atmosphère file names (40 and 64
hex digits), since which one is matched depends on the Atmosphère version.

## Publishing a change

1. Add, change or delete files here.
2. `python3 tools/patches_manifest.py` regenerates `manifest.json` and bumps
   its serial. CI fails if you forget.
3. Commit and push to `main`.
4. `tools/publish-patches.sh` creates the `patches-<serial>` release.

The release carries `manifest.json`, every file as `<sha256>.ips`, and
`patches.zip` holding the same files, which the Toolbox downloads in one
request. The GRID0+ server mirrors `manifest.json` and `patches.zip` under
`/grid0plus/patches/` on the Toolbox API port, for when GitHub cannot be
reached.

Nothing reaches a console or an emulator before step 4. Deleting a patch here
removes it from devices on their next check, because clients remove files
they installed that the manifest no longer lists.
