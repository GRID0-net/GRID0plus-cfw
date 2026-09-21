// SwitchNet Toolbox — certificate trust provisioning.
//
// A private server needs the console to trust its TLS certificate. This
// module mirrors romfs:/sd/... onto the SD card, which installs:
//
//  - SwitchNet's root CA (the actual certificate, embedded at build time)
//    at the paths the browser applet's CA bundle and WebView-based account
//    linking read from, plus a reference copy under sdmc:/switchnet/certs/.
//  - The public disable_ca_verification / disable_browser_ca_verification
//    ExeFS/NRO IPS patches (from misson20000/exefs_patches — one file per
//    firmware build ID) that make the system SSL service and the browser
//    applet skip the stock CA check. Without these, installing the CA alone
//    is not enough: the browser still rejects a self-signed certificate, and
//    account linking fails to load (this is what error 2123-0308 during
//    account linking generally means).
//
// Atmosphère only applies a patch whose build-ID filename matches the
// firmware actually running, so shipping patches for every supported
// firmware is harmless on any given console — unmatched ones simply sit
// unused.
#ifndef SWITCHNET_CERTS_H
#define SWITCHNET_CERTS_H

#include <stdbool.h>

// Mirrors every file under romfs:/sd/ onto the SD card at the corresponding
// sdmc:/ path, creating directories as needed and overwriting existing
// files. Requires romfsInit() to already be active.
bool certs_provision(void);

// Removes every file certs_provision() would install (by walking the same
// romfs:/sd/ tree), and prunes any directory left empty by that removal —
// never one that still holds something else (e.g. other exefs_patches).
//
// Also unconditionally removes a fixed, append-only list of every path a
// PAST released version is known to have written (see certs.c), even if the
// currently-running build's own romfs:/sd/ tree no longer mentions that
// path. Without this, someone who installed an old build, then later
// updated the .nro itself without ever running "Switch to Default" first,
// could accumulate files a newer build's tree-mirror can no longer see to
// clean up — orphaned, not wrong-content (copyFile always overwrites
// same-path files), but still SwitchNet's to remove.
void certs_remove(void);

#endif // SWITCHNET_CERTS_H
