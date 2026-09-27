// GRID0+ Toolbox — self-updater via GitHub Releases.
//
// On demand (when the user selects "Check for updates"), queries GitHub's
// releases API, compares the tag's semver against SWITCHNET_VERSION_*, and if
// newer downloads the .nro release asset and replaces the file this app is
// currently running from.
#ifndef SWITCHNET_UPDATE_H
#define SWITCHNET_UPDATE_H

#include <stdbool.h>

typedef struct {
    bool available;
    int maj, min, patch;
    long size;   // expected .nro size, for a post-download integrity check
    // 0 when the check completed (available says whether there is an update);
    // otherwise a NET_ERR_* code or the HTTP status the server answered with.
    int error;
} SwitchnetUpdate;

typedef enum {
    SWITCHNET_UPDATE_OK = 0,
    SWITCHNET_UPDATE_NET_FAIL,
    SWITCHNET_UPDATE_SIZE_FAIL,
    SWITCHNET_UPDATE_WRITE_FAIL,
} SwitchnetUpdateResult;

typedef enum {
    SWITCHNET_UPDATE_PHASE_DOWNLOAD = 0,
    SWITCHNET_UPDATE_PHASE_INSTALL = 1,
} SwitchnetUpdatePhase;

typedef void (*SwitchnetUpdateProgressFn)(SwitchnetUpdatePhase phase, long done, long total);

// Path of the .nro currently running (hbmenu passes it in argv[0]) — this is
// the file a successful update replaces. Call once at startup, before any
// update check. Safe to call with NULL/empty (falls back to a fixed path).
void update_set_self_path(const char *argv0);

// Queries GitHub for the latest release. Managed sockets/SSL internally.
SwitchnetUpdate update_check(void);

// Downloads and installs the update found by the last update_check() call
// that reported one available. `onProgress` may be NULL.
SwitchnetUpdateResult update_apply(long expectedSize, SwitchnetUpdateProgressFn onProgress);

#endif // SWITCHNET_UPDATE_H
