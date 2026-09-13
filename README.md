# SwitchNet Toolbox

A small Nintendo Switch homebrew (`.nro`) that switches an Atmosphère console
between the **SwitchNet** network and its **Default** (previous/original)
hosts configuration — the same mechanism used by network-switcher homebrew
like Prelude, without any bundled game mods.

```
SwitchNet Toolbox  v1.0.0
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
- **On-demand auto-updater.** "Check for updates" queries this repository's
  GitHub releases, and if a newer `.nro` is tagged, downloads and installs it
  over the file you're currently running. It never phones home on its own —
  only when you press the button.
- **IP override.** "Set custom SwitchNet IP" opens the on-screen keyboard to
  point at a different server (e.g. a local instance for debugging); "Reset
  IP to default" goes back to `9.205.104.23`.

## How it works

| What | Where |
| --- | --- |
| Host redirections | `/atmosphere/hosts/{sysmmc,emummc}.txt` (Atmosphère DNS-MITM) |
| DNS-MITM on/off | `/atmosphere/config/system_settings.ini` |
| Hosts backup | `sdmc:/switchnet/hosts_backup/` (mirrors the whole hosts folder) |
| App settings (IP, flags) | `sdmc:/switchnet/config.cfg` |
| Certificates | `sdmc:/rootCA.pem`, `sdmc:/switchnet/certs/`, and the console's browser CA bundle under `sdmc:/atmosphere/contents/0100000000000803/romfs/browser/` |
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

## Certificates

This app installs SwitchNet's actual root CA certificate (`CN=SwitchNet
Local CA`, embedded at build time) at the paths a working install needs:

- `romfs/certs/switchnet_root_ca.pem` → `sdmc:/switchnet/certs/switchnet_root_ca.pem`
- `romfs/certs/rootCA.pem` → `sdmc:/rootCA.pem`
- `romfs/certs/browser/RootCaEtc.pem` / `RootCaSdkAdditional.pem` → the
  console's browser-applet CA bundle, for WebView-based account linking

To rotate the certificate later: replace the four files under
`romfs/certs/` with the new PEM-encoded root CA, bump `APP_VERSION`, and push
to `main` — CI builds and releases it, and existing installs pick it up
through the in-app updater. Note that getting a *game's own* SSL stack (as
opposed to the browser/WebView) to accept this CA typically also requires
firmware-build-specific ExeFS/NRO IPS patches to skip certificate
verification; those are a separate piece of work and aren't included here.

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
