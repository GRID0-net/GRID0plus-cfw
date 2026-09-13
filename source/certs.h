// SwitchNet Toolbox — certificate trust provisioning.
//
// A private server needs the console to trust its TLS certificate. On real
// Atmosphère consoles that normally takes firmware-build-specific ExeFS/NRO
// IPS patches (to skip the system CA check) plus a browser-trusted root CA
// for WebView-based account linking — both of which have to be generated
// against SwitchNet's actual certificate and a matched set of firmware
// build IDs.
//
// Until those are provided, this module ships and installs PLACEHOLDER files
// at the exact paths the real ones will occupy, so the rest of the apply/
// restore flow (and this app's update mechanism for refreshing them later)
// is already wired up. Replace the files under romfs/certs/ with the real
// SwitchNet certificate material and this code does not need to change.
#ifndef SWITCHNET_CERTS_H
#define SWITCHNET_CERTS_H

#include <stdbool.h>

// Copies the (currently stub) certificate files from romfs:/certs/ to their
// target locations on the SD card. Requires romfsInit() to already be active.
bool certs_provision(void);

// Removes every file certs_provision() installs, and prunes any directory
// left empty by that removal (never a directory that still holds something
// else, such as /atmosphere/contents itself).
void certs_remove(void);

#endif // SWITCHNET_CERTS_H
