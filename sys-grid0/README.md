# sys-GRID0

A port of ZeroTier with the GRID0 network to the nintendo switch as a sysmodule, very AI assisted in the making, yet extremely functional with better results than i had hoped. Get the latest release from the releases page, or read the build instructions in build.md

## where sys-GRID0 is at right now

The whole point of this was to make native LAN play work over the internet
without needing a PC relay or another console sitting on the same network. We
have reached that point, and it has worked in actual matches.

Here is what is working at the moment:

- ZeroTier runs as a Horizon sysmodule, joins a network, keeps its identity and
  managed IP, and survives normal sleep and wake.
- The virtual IPv4 side handles ARP, IPv4, UDP and ICMP. The host tests are at
  88/88, with fuzz testing of the packet path as well.
- The `bsd:u` and `nifm:u` MITMs are doing their job. LAN discovery and game
  sockets can be sent through ZeroTier while ordinary Switch networking keeps
  working too.
- Mario Kart 8 Deluxe, Splatoon 2 and Splatoon 3 have all managed LAN play over
  ZeroTier with Switch and Ryujinx players. The other games in the whitelist
  are ready for people to try and report back on.
- There is now a proper Ultrahand overlay. It shows the current status, lets a
  user pick a saved network or type the entire 16-digit network ID, applies a
  network change without rebooting, and has switches for the sysmodule, BSD,
  NIFM and detailed logging.
- New installs turn on the sysmodule and both MITMs automatically. Saved
  networks live in `/config/sys-GRID0/networks.ini`, so an update does not
  wipe them out.
- A lot of memory work and logging cleanup has gone into keeping the module
  alive on real hardware. The larger BSD/NIFM logs are optional; the normal
  build keeps the small boot, status and uplink logs running.

## current limitations

The latest test build adds game-exit/reinitialization cleanup and automatic
UPnP/NAT-PMP router mapping. Those changes still need repeated-session testing
on hardware; see [the test notes](LIFECYCLE_NAT_TESTING.md). Router mapping can
help restrictive connections, but cannot promise a direct path through CGNAT
or networks that block UDP.

There are still things left to do. Games that only support local wireless mode
need an `ldn:u` MITM before they can use ZeroTier. The LAN whitelist covers the
games tested so far, but more compatibility testing is welcome. If something
breaks, include `status.txt`, `uplink.log`, and (when enabled) `bsd.log` and
`nifm.log` with the report so it can actually be investigated.

## the important folders

```text
source/                 sysmodule, ZeroTier port and LAN MITMs
source/net/             virtual IPv4 network and packet handling
overlay/                Ultrahand/Tesla overlay
compat/                 Horizon and dependency compatibility shims
patches/                small Horizon-specific ZeroTier patch
tests/                  host-side VNet tests and packet reference data
Atmosphere-libs/        pinned Atmosphère dependency submodule
ZeroTierOne/            pinned ZeroTier dependency submodule
```

For prerequisites, submodules, build outputs, installation, and tests, see
[build.md](build.md).

## licensing

The ZeroTier `node/` sources are MPL-2.0. Atmosphère/libstratosphere and the
other bundled dependencies retain their upstream licenses. The port, shim and
overlay code should be distributed with the license terms of the project as it
is released; do not add ZeroTier's separately licensed `libzt` component.

## Resource-pressure safety

The module shares Horizon's System resource group with essential services.
`am` aborting with 2001-0132 identifies resource exhaustion, but does not by
itself distinguish physical memory from threads, sessions or events. Keep
`boot.log` and `uplink.log` when reporting a failure.

Startup and each 2 MiB ZeroTier identity workspace request now require 2 MiB
of additional free System memory. The run loop checks that safety floor every
five seconds. Insufficient or unreadable headroom makes the optional module
exit and release its resources rather than hold its arena indefinitely. LAN
connectivity stops for that boot; the log records `safe-stop` and the measured
budget. Failure to release the identity heap also stops the process, and node
thread creation failure no longer invokes a fatal abort.

This floor is based on the 2 MiB display reservation seen in earlier fatal
reports. It is a mitigation, not a guarantee: other processes can allocate
between samples, and another resource kind can still run out. Actual console
boot, LAN-play and sleep/wake tests are required. The known-bad 1 MiB allocator
and 128 KiB node stack have not been reinstated, and identity verification
still uses the protocol-required 2 MiB workspace.

Toolbox 0.8.4 offers an explicit sysmodule/overlay update choice. Updating does
not enable a disabled module or replace its configuration and identity. Only
deliberately enable its boot flag; do not restore autostart merely to obtain an
updated binary. Reboot to load the installed replacement.

MITM registration is now undone if its server thread cannot be created, so
clients are not left waiting on a port with no worker. A linked-build audit
on 2026-10-08 measured about 1.04 MiB text, 106 KiB data and 2.41 MiB BSS.
The BSS includes the 1,152 KiB arena, 256 KiB node stack, and 592 KiB Splatoon 2
proxy buffer. The latter is resident even outside Splatoon 2 and is a candidate
for a future on-demand allocation design; moving it into the current arena
without accounting for fragmentation would repeat earlier allocation crashes.
No smaller steady-state footprint is claimed for the safety guard itself.

Failed node startup now releases the ZeroTier node and bound UDP socket before
retrying, including arena refusal and join failure. The host fault-injection
tests exercise the real initialization bodies with service/kernel stubs; they
verify cleanup, not Horizon or ZeroTier behavior. Memory-headroom checks also
remain active while waiting for configuration and retrying startup, rather
than only after reaching the run loop.

The optional PSC monitor stays disabled by default. If its worker cannot be
created after registration, it unregisters and destroys its local object;
failed unregistration stops the process rather than retaining a power-state
participant with no thread to acknowledge requests. Console sleep/wake testing
is still required before enabling this feature.

A hardware test of the memory guard on 2026-10-08 exposed an unsafe process
exit: `report_00000000c7ffcd2e.bin` identifies SM (`0100000000000004`) aborting
with `SessionClosed` immediately after our runtime memory stop. Atmosphere's
SM aborts on failure of MITM query command 65000. All deliberate exit paths now
uninstall registered BSD/NIFM interceptions before closing query responders.
If unregistration fails, the responders stay alive while cleanup is retried.
This fixes the shutdown ordering; it does not make the memory floor a proof
that ZeroTier can stay running alongside every other sysmodule. A hardware
retest is still needed. Source: Atmosphere `stratosphere/sm/source/impl/`
`sm_service_manager.cpp`, `GetMitmServiceHandleImpl` and `UninstallMitm`.
