// GRID0+ Toolbox, certificate trust provisioning.
//
// A private server needs the console to trust its TLS certificate. This
// module mirrors romfs:/sd/... onto the SD card, which installs:
//
//  - GRID0+'s root CA (the actual certificate, embedded at build time)
//    at the paths the browser applet's CA bundle and WebView-based account
//    linking read from, plus a reference copy under sdmc:/GRID0plus/certs/.
//
// The IPS patches that make the system SSL service and the browser applet
// accept that CA used to ship here too; they are now downloaded by
// patches.c, from the same releases as every other patch.
#ifndef GRID0PLUS_CERTS_H
#define GRID0PLUS_CERTS_H

#include <stdbool.h>

// Mirrors every file under romfs:/sd/ onto the SD card at the corresponding
// sdmc:/ path, creating directories as needed and overwriting existing
// files. Requires romfsInit() to already be active.
bool certs_provision(void);

// Removes every file certs_provision() would install (by walking the same
// romfs:/sd/ tree), and prunes any directory left empty by that removal ,
// never one that still holds something else (e.g. other exefs_patches).
//
// Also unconditionally removes a fixed, append-only list of every path a
// PAST released version is known to have written (see certs.c), even if the
// currently-running build's own romfs:/sd/ tree no longer mentions that
// path. Without this, someone who installed an old build, then later
// updated the .nro itself without ever running "Switch to Default" first,
// could accumulate files a newer build's tree-mirror can no longer see to
// clean up, orphaned, not wrong-content (copyFile always overwrites
// same-path files), but still GRID0+'s to remove.
void certs_remove(void);

// Deletes `dir` and everything under it. Only for directories that are
// exclusively GRID0+'s.
void grid0plus_remove_dir_and_contents(const char *dir);

#endif // GRID0PLUS_CERTS_H
