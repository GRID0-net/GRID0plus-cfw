# GRID0+ Toolbox

A small Nintendo Switch homebrew (`.nro`) that switches an Atmosphère console
between the **GRID0+** network and its **Default** (previous/original)
hosts configuration, the same mechanism used by network-switcher homebrew
like Prelude, without any bundled game mods.

```
GRID0+ Toolbox  <VERSION>
========================================

Current mode : DEFAULT
GRID0+ IP : 89.168.58.206

> Switch to GRID0+
  Switch to Default (restore original hosts)
  Set custom GRID0+ IP
  Reset IP to default (89.168.58.206)
  Back up hosts folder now
  Restore backup on Default mode: ON
  Check for updates
  Server status
  Exit
```

## Features

- **Mode switch.** *GRID0+* mode redirects the usual Nintendo online
  hostnames to a GRID0+ server and reboots so Atmosphère's DNS-MITM picks
  it up; *Default* mode removes that redirection and reboots back.
- **Full hosts backup.** Before GRID0+ mode ever writes to
  `/atmosphere/hosts` for the first time, every file already in that folder
  is copied to `sdmc:/GRID0plus/hosts_backup/`, not just the files this app
  manages, so a different community server's redirections or your own
  blocklist survive. "Back up hosts folder now" repeats this on demand, and
  "Switch to Default" restores it (toggle: "Restore backup on Default mode").
- **Certificate provisioning (stubs for now).** GRID0+'s certificate
  material is copied into place alongside the hosts change. The actual
  certificate isn't included yet, see [Certificates](#certificates) below.
- **On-demand auto-updater.** "Check for updates" asks GRID0+'s own
  toolbox API (not GitHub directly, this repo is private, so an
  unauthenticated request for its releases would just 404) whether a newer
  `.nro` is tagged, and if so downloads and installs it over the file you're
  currently running. It never phones home on its own, only when you press
  the button. See [Updates & server status](#updates--server-status) below.
- **Server status.** Shows a coarse ok/degraded/down/unknown for each
  GRID0+ service (dauth, aauth, baas, stubs, npln, dashboard, dns,
  natcheck), from the same toolbox API. No error detail, no credential
  needed, just enough to tell "GRID0+ is down" from "my own network is
  the problem".
- **IP override.** "Set custom GRID0+ IP" opens the on-screen keyboard to
  point at a different server (e.g. a local instance for debugging); "Reset
  IP to default" goes back to `89.168.58.206`. Both the updater and the
  status screen follow this same address.

## How it works

| What | Where |
| --- | --- |
| Host redirections | `/atmosphere/hosts/{sysmmc,emummc}.txt` (Atmosphère DNS-MITM) |
| DNS-MITM on/off | `/atmosphere/config/system_settings.ini` |
| Hosts backup | `sdmc:/GRID0plus/hosts_backup/` (mirrors the whole hosts folder) |
| App settings (IP, flags) | `sdmc:/GRID0plus/config.cfg` |
| Certificates & CA-bypass patches | `romfs/sd/` mirrored onto the SD card root, `rootCA.pem`, the browser CA bundle, and the `exefs_patches`/`nro_patches` below |
| Debug trace | `sdmc:/GRID0plus/trace.txt` |

Because it only writes files Atmosphère (and the console's browser applet)
read at boot/launch, everything is reversible by switching modes or deleting
`sdmc:/GRID0plus/` and the files listed above by hand.

The generated hosts file redirects the common Nintendo online endpoints
(accounts, `*.srv.nintendo.net`, the NEX secure-server wildcard, the browser
connectivity check) to the configured GRID0+ IP, and null-routes
telemetry in both modes. It does not contain per-game server IDs or bundled
mods, extend `source/hosts.c` (`hosts_build`) as GRID0+'s own
infrastructure grows.

## Certificates & certificate trust

`romfs/sd/` is a mirror of the SD card root: everything under it gets copied
onto the SD card, at the same relative path, when GRID0+ mode is applied
(and removed again in Default mode). It currently contains:

- **GRID0+'s actual root CA** (`CN=GRID0+ Local CA`, embedded at build
  time) at `grid0plus/certs/grid0plus_root_ca.pem`, `rootCA.pem`, and the
  console's browser-applet CA bundle under
  `atmosphere/contents/0100000000000803/romfs/browser/`.
- **`disable_ca_verification`** (`atmosphere/exefs_patches/`) and
  **`disable_browser_ca_verification`** (`atmosphere/nro_patches/`), the
  public per-firmware-build-ID IPS patches from
  [misson20000/exefs_patches](https://github.com/misson20000/exefs_patches)
  that make the system SSL service and the browser applet accept a
  self-signed certificate. **Installing the CA alone is not enough**, the
  browser and system SSL service still enforce the stock CA check without
  these, which is what error **2123-0308** (browser fails to open during
  account linking) generally means. Atmosphère only applies the patch whose
  filename matches the running firmware's build ID, so shipping patches for
  every supported firmware is harmless.
- **`bcat_signature_bypass`**, one patch, for the `bcat` sysmodule build
  `6D9772A7…` that firmware **22.5.0** ships. It replaces the boolean BCAT
  stores from its central RSA verification callback, that callback's own
  result, with a constant true, which is what lets the console accept the
  delivery-cache response GRID0+ signs itself rather than refusing content
  Nintendo's key never touched. Unlike the two sets above this one is not
  from upstream: it is GRID0+'s own patch, for this one build, out of the
  operator's own dump, and it neither creates nor impersonates a Nintendo
  signature. A firmware update needs a new one, the build ID then doesn't
  match, so Atmosphère applies nothing and BCAT falls back to the 304
  answers. It only matters if the console actually reaches GRID0+ for the
  three `bcat-*` CDN hosts, which this app's hosts file already routes
  there; the server side (real delivery-cache files under `bcat.seed_dir`,
  or it keeps answering 304) is written up in GRID0+'s
  `internal/bcat/README.md`.

To rotate the certificate later: replace the PEM files under `romfs/sd/`
with the new root CA, bump `APP_VERSION`, and push to `main`, CI builds and
releases it, and existing installs pick it up through the in-app updater.

## Updates & server status

This repository is **private**. GitHub's API answers an unauthenticated
`releases/latest` request against a private repo with a 404, and the
alternative, putting a GitHub token in the `.nro` itself, would make that
credential permanently extractable from every copy in the field (`strings`
on the binary is all it takes). So the app never talks to GitHub at all:

```
Console  ──GET /updates/latest──►  GRID0+'s toolbox API  ──(token)──►  GitHub
Console  ◄──tag/url/size──────────         (same shape a real GitHub response has)
Console  ──GET /updates/download──►  toolbox API  ──(token)──►  GitHub release asset
```

`source/net.c`/`source/update.c` reach this at `g_server_ip` (the same
address/override the hosts screen uses) on `GRID0PLUS_TOOLBOX_PORT` (8443,
`source/config.h`), a dedicated port on the GRID0+ server's nginx edge,
not a redirected Nintendo hostname, so no DNS entry is needed for it. The
"Server status" screen (`source/status.c`) talks to the same host/port,
`/status` instead of `/updates/*`.

The server side of this, `internal/toolbox` and `cmd/toolbox` in the
(also private) `switchnet` repository, holds the actual GitHub credential
and is what makes both features work; there is nothing further to configure
in this repository for them.

## Building

Requires [devkitPro](https://devkitpro.org/) with the `switch-dev` package,
or Docker:

```sh
docker run --rm -v "$PWD:/work" -w /work devkitpro/devkita64 make -j$(nproc)
```

The result is `grid0plus.nro`, copy it to `/switch/` on your SD card.

## Disclaimer

This project is not affiliated with, endorsed by, or connected to Nintendo.
"Nintendo" and "Nintendo Switch" are trademarks of Nintendo. Use it only on
hardware you own, running Atmosphère custom firmware you've set up yourself.
