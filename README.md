# SwitchNet Toolbox

A small Nintendo Switch homebrew (`.nro`) that switches an Atmosphère console
between the **SwitchNet** network and its **Default** (previous/original)
hosts configuration — the same mechanism used by network-switcher homebrew
like Prelude, without any bundled game mods.

```
SwitchNet Toolbox  beta-0.3.0
========================================

Current mode : DEFAULT
SwitchNet IP : 9.205.104.23

> Switch to SwitchNet
  Switch to Default (restore original hosts)
  Set custom SwitchNet IP
  Reset IP to default (9.205.104.23)
  Back up hosts folder now
  Restore backup on Default mode: ON
  Check for updates
  Server status
  Exit
```

## Features

- **Mode switch.** *SwitchNet* mode redirects the usual Nintendo online
  hostnames to a SwitchNet server and reboots so Atmosphère's DNS-MITM picks
  it up; *Default* mode removes that redirection and reboots back.
- **Full hosts backup.** Before SwitchNet mode ever writes to
  `/atmosphere/hosts` for the first time, every file already in that folder
  is copied to `sdmc:/switchnet/hosts_backup/` — not just the files this app
  manages, so a different community server's redirections or your own
  blocklist survive. "Back up hosts folder now" repeats this on demand, and
  "Switch to Default" restores it (toggle: "Restore backup on Default mode").
- **Certificate provisioning (stubs for now).** SwitchNet's certificate
  material is copied into place alongside the hosts change. The actual
  certificate isn't included yet — see [Certificates](#certificates) below.
- **On-demand auto-updater.** "Check for updates" asks SwitchNet's own
  toolbox API (not GitHub directly — this repo is private, so an
  unauthenticated request for its releases would just 404) whether a newer
  `.nro` is tagged, and if so downloads and installs it over the file you're
  currently running. It never phones home on its own — only when you press
  the button. See [Updates & server status](#updates--server-status) below.
- **Server status.** Shows a coarse ok/degraded/down/unknown for each
  SwitchNet service (dauth, aauth, baas, stubs, npln, dashboard, dns,
  natcheck), from the same toolbox API. No error detail, no credential
  needed — just enough to tell "SwitchNet is down" from "my own network is
  the problem".
- **IP override.** "Set custom SwitchNet IP" opens the on-screen keyboard to
  point at a different server (e.g. a local instance for debugging); "Reset
  IP to default" goes back to `9.205.104.23`. Both the updater and the
  status screen follow this same address.

## How it works

| What | Where |
| --- | --- |
| Host redirections | `/atmosphere/hosts/{sysmmc,emummc}.txt` (Atmosphère DNS-MITM) |
| DNS-MITM on/off | `/atmosphere/config/system_settings.ini` |
| Hosts backup | `sdmc:/switchnet/hosts_backup/` (mirrors the whole hosts folder) |
| App settings (IP, flags) | `sdmc:/switchnet/config.cfg` |
| Certificates & CA-bypass patches | `romfs/sd/` mirrored onto the SD card root — `rootCA.pem`, the browser CA bundle, and the `exefs_patches`/`nro_patches` below |
| Debug trace | `sdmc:/switchnet/trace.txt` |

Because it only writes files Atmosphère (and the console's browser applet)
read at boot/launch, everything is reversible by switching modes or deleting
`sdmc:/switchnet/` and the files listed above by hand.

The generated hosts file redirects the common Nintendo online endpoints
(accounts, `*.srv.nintendo.net`, the NEX secure-server wildcard, the browser
connectivity check) to the configured SwitchNet IP, and null-routes
telemetry in both modes. It does not contain per-game server IDs or bundled
mods — extend `source/hosts.c` (`hosts_build`) as SwitchNet's own
infrastructure grows.

## Certificates & certificate trust

`romfs/sd/` is a mirror of the SD card root: everything under it gets copied
onto the SD card, at the same relative path, when SwitchNet mode is applied
(and removed again in Default mode). It currently contains:

- **SwitchNet's actual root CA** (`CN=SwitchNet Local CA`, embedded at build
  time) at `switchnet/certs/switchnet_root_ca.pem`, `rootCA.pem`, and the
  console's browser-applet CA bundle under
  `atmosphere/contents/0100000000000803/romfs/browser/`.
- **`disable_ca_verification`** (`atmosphere/exefs_patches/`) and
  **`disable_browser_ca_verification`** (`atmosphere/nro_patches/`) — the
  public per-firmware-build-ID IPS patches from
  [misson20000/exefs_patches](https://github.com/misson20000/exefs_patches)
  that make the system SSL service and the browser applet accept a
  self-signed certificate. **Installing the CA alone is not enough** — the
  browser and system SSL service still enforce the stock CA check without
  these, which is what error **2123-0308** (browser fails to open during
  account linking) generally means. Atmosphère only applies the patch whose
  filename matches the running firmware's build ID, so shipping patches for
  every supported firmware is harmless.

To rotate the certificate later: replace the PEM files under `romfs/sd/`
with the new root CA, bump `APP_VERSION`, and push to `main` — CI builds and
releases it, and existing installs pick it up through the in-app updater.

## Updates & server status

This repository is **private**. GitHub's API answers an unauthenticated
`releases/latest` request against a private repo with a 404, and the
alternative — putting a GitHub token in the `.nro` itself — would make that
credential permanently extractable from every copy in the field (`strings`
on the binary is all it takes). So the app never talks to GitHub at all:

```
Console  ──GET /updates/latest──►  SwitchNet's toolbox API  ──(token)──►  GitHub
Console  ◄──tag/url/size──────────         (same shape a real GitHub response has)
Console  ──GET /updates/download──►  toolbox API  ──(token)──►  GitHub release asset
```

`source/net.c`/`source/update.c` reach this at `g_server_ip` (the same
address/override the hosts screen uses) on `SWITCHNET_TOOLBOX_PORT` (8443,
`source/config.h`) — a dedicated port on the SwitchNet server's nginx edge,
not a redirected Nintendo hostname, so no DNS entry is needed for it. The
"Server status" screen (`source/status.c`) talks to the same host/port,
`/status` instead of `/updates/*`.

The server side of this — `internal/toolbox` and `cmd/toolbox` in the
(also private) `switchnet` repository — holds the actual GitHub credential
and is what makes both features work; there is nothing further to configure
in this repository for them.

## Building

Requires [devkitPro](https://devkitpro.org/) with the `switch-dev` package,
or Docker:

```sh
docker run --rm -v "$PWD:/work" -w /work devkitpro/devkita64 make -j$(nproc)
```

The result is `switchnet.nro` — copy it to `/switch/` on your SD card.

Releases are fully automatic: the GitHub Actions workflow builds every push
to `main`, and a push that bumps `APP_VERSION` (in the `Makefile`, kept in
lockstep with `SWITCHNET_VERSION_*` in `source/version.h` — the CI job checks
this) cuts a new GitHub release for that version, tagged `vX.Y.Z`, with the
`.nro` attached. No manual tagging needed; re-running CI against a version
that's already released is a no-op. The project is currently in its beta
phase (major version `0`) — release titles read `beta-MAJOR.MINOR` and the
in-app header does too, until it graduates to `1.0.0`.

## Disclaimer

This project is not affiliated with, endorsed by, or connected to Nintendo.
"Nintendo" and "Nintendo Switch" are trademarks of Nintendo. Use it only on
hardware you own, running Atmosphère custom firmware you've set up yourself.
