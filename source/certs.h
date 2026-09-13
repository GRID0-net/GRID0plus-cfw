// SwitchNet Toolbox — certificate trust provisioning.
//
// A private server needs the console to trust its TLS certificate. This
// module installs SwitchNet's root CA (romfs/certs/*.pem — the actual
// certificate, embedded at build time) at the paths the browser applet's CA
// bundle and WebView-based account linking read from.
//
// Not covered here: on real Atmosphère consoles, getting a game's own SSL
// stack to accept a private server's certificate typically also needs
// firmware-build-specific ExeFS/NRO IPS patches (to skip the system CA
// check), generated against a matched set of firmware build IDs. That's a
// separate piece of work from installing the CA itself and isn't included.
#ifndef SWITCHNET_CERTS_H
#define SWITCHNET_CERTS_H

#include <stdbool.h>

// Copies SwitchNet's root CA files from romfs:/certs/ to their target
// locations on the SD card. Requires romfsInit() to already be active.
bool certs_provision(void);

// Removes every file certs_provision() installs, and prunes any directory
// left empty by that removal (never a directory that still holds something
// else, such as /atmosphere/contents itself).
void certs_remove(void);

#endif // SWITCHNET_CERTS_H
