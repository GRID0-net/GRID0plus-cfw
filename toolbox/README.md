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
  On an Erista (V1, RCM) console the reboot goes back into hekate, loaded
  from `bootloader/update.bin` (or `atmosphere/reboot_payload.bin`), so the
  console comes back up in CFW instead of stock firmware. Mariko consoles,
  or an SD card with neither payload, get a normal reboot.
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
- **`s3smallmatch_bypass`**, one patch, for Splatoon 3's main build
  `28C4287A…`. It lets a matchmade game start with fewer than eight players,
  which GRID0+'s queue does after a short wait. The game refuses an
  undersized match in three places, each a player count compared with the
  full size: the wait for the room to fill (`WaitFullmember`,
  `0x71038A420C`), the minimum-player check that raises
  `MatchSyncDisconnect1` (`0x71032C1C7C`), and the member-count check that
  raises `ShareMemberNumInvalid3` (`0x7103898160`). Each compare is turned
  into one that always matches. Those three checks were confirmed on hardware
  with two consoles, on one network and across two. Version 0.8.1 additionally
  lowers two lobby-controller minimum checks (`0x710389F750`,
  `0x710389F864`) to two players. The controller defaults to eight and otherwise
  cannot advance its stable-player counter in an undersized retained lobby.
  The stability check and agreement between the ready/current player counts
  remain intact; asynchronous rejoin success is not forced. These additional
  sites allowed an undersized rematch on hardware. Version 0.8.2 additionally
  gates the shared controller at `0x710389F654` on the active Gamesync
  session’s cached `ebf`: primary at manager+41, secondary at manager+73.
  While backfill is open it stays in the lobby; GRID0 closes it after the
  grace window. The original conditional branch, NZCV, count stability and
  readiness checks remain intact. Four verified alignment spans hold the
  trampoline, without changing fresh/private lobby minimums. The new
  server-lock gate still needs a hardware test.
  Version 0.8.5 lets Tricolor start with one player per team. Its first step
  (`fest_match_tricolor_team_config`) gathers a pair of one fest team and
  waits for exactly two players, in the lobby wait (`0x71038AAC84`) and again
  in the delegation step that hands the pair to the oneshot queue
  (`0x71038AF188`, `MatchDelegateDisconnect` otherwise). Both equality
  branches become less-or-equal, so one or two players pass and three still
  do not. Confirmed on hardware with one player per team (Switch, Ryujinx,
  Citron) on 2026-10-09.
  Regenerate with `python3 tools/make_s3_smallmatch.py [main.flat]`; the optional
  dump verifies every original instruction. A game update needs a new patch.
- **`s3moveless_bypass`**, one patch, for the same Splatoon 3 build. The game
  ends a battle for a player whose sticks, buttons and speed stay unchanged
  for a while (`PlayerMoveless`); it kicked emulator players who were moving,
  and idle test devices in every test match. The idle counter at
  `0x7103B09CB8` still runs, but the branch that raises the error at
  `0x7103B09E64` becomes unconditional, so it is never raised. Regenerate with
  `python3 tools/make_s3_moveless.py [main.flat]`.
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

The in-app updater reads GitHub's public release list, including sysmodule-only
`-sys.N` releases, and offers **Toolbox only**, **sys-GRID0+ and overlay only**,
or **Both**. It downloads the selected release's combined ZIP using its exact
tag. Components already current are left alone when selecting Both.
Settings, identities, hosts and the sysmodule boot flag are never extracted.
Installing a previously absent sysmodule does not automatically enable it.
Sysmodule updates take effect after a reboot; Toolbox updates after relaunch.
The last installed sysmodule bundle is tracked in
`/GRID0plus/sys-grid0-installed-release.txt`; an older manual installation has
no marker and is offered an update once. This records what is installed on SD,
not which binary is currently running.

No token is needed: the repository is public. The requests use HTTP/1.0 so
GitHub answers with a plain `Content-Length` body; `source/net.c` does not
decode chunked transfer encoding. ZIP entries are staged and checked for size
and CRC before replacement. The previous binary is retained beside its replacement as
`.grid0-update.bak`; failed placement attempts restore it. The whole multi-file
installation is not transactional if power or the SD fails during renames.

Before 0.7.0 the repository was private and updates went through GRID0+'s
own `/updates/latest` relay, which held a GitHub token; that relay is how a
0.6 install reaches 0.7.

The "Server status" screen (`source/status.c`) still talks to the GRID0+
server, at `g_server_ip` on `GRID0PLUS_TOOLBOX_PORT` (8443,
`source/config.h`), `/status`.

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

### Upgrading older Toolbox versions

Version 0.8.3 temporarily restricted updates to the Toolbox. Version 0.8.4
replaces that freeze with the explicit component selector. An older updater
still follows its own behavior: manually replace only
`/switch/grid0plus-toolbox.nro` to get this selector without unintentionally
installing or enabling the sysmodule. Future checks use the choices above.

After applying GRID0+, the hosts header includes the generating Toolbox
version. Updating the NRO alone does not rewrite hosts: run Apply GRID0+ to
regenerate them, then reboot to make Atmosphere load them.
