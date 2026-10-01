// GRID0+ Toolbox, self-updater via GitHub Releases.
//
// On demand (when the user selects "Check for updates"), queries GitHub's
// releases API, compares the tag's semver against GRID0PLUS_VERSION_*, and if
// newer downloads the .nro release asset and replaces the file this app is
// currently running from.
#ifndef GRID0PLUS_UPDATE_H
#define GRID0PLUS_UPDATE_H

#include <stdbool.h>

typedef struct {
    bool available;
    int maj, min, patch;
    long size;   // expected .nro size, for a post-download integrity check
    // 0 when the check completed (available says whether there is an update);
    // otherwise a NET_ERR_* code or the HTTP status the server answered with.
    int error;
} Grid0plusUpdate;

typedef enum {
    GRID0PLUS_UPDATE_OK = 0,
    GRID0PLUS_UPDATE_NET_FAIL,
    GRID0PLUS_UPDATE_SIZE_FAIL,
    GRID0PLUS_UPDATE_WRITE_FAIL,
} Grid0plusUpdateResult;

typedef enum {
    GRID0PLUS_UPDATE_PHASE_DOWNLOAD = 0,
    GRID0PLUS_UPDATE_PHASE_INSTALL = 1,
} Grid0plusUpdatePhase;

typedef void (*Grid0plusUpdateProgressFn)(Grid0plusUpdatePhase phase, long done, long total);

// Path of the .nro currently running (hbmenu passes it in argv[0]), this is
// the file a successful update replaces. Call once at startup, before any
// update check. Safe to call with NULL/empty (falls back to a fixed path).
void update_set_self_path(const char *argv0);

// Queries GitHub for the latest release. Managed sockets/SSL internally.
Grid0plusUpdate update_check(void);

// Downloads and installs the update found by the last update_check() call
// that reported one available. `onProgress` may be NULL.
Grid0plusUpdateResult update_apply(long expectedSize, Grid0plusUpdateProgressFn onProgress);

#endif // GRID0PLUS_UPDATE_H
